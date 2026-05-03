import json
import logging
import os
import queue
import ssl
import threading
import time
from contextlib import asynccontextmanager
from datetime import datetime, timezone
from typing import Any, Optional

import paho.mqtt.client as mqtt
from fastapi import FastAPI, HTTPException, Depends
from fastapi.security import APIKeyHeader
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field
from sqlalchemy import Column, DateTime, Float, Integer, String, create_engine
from sqlalchemy.exc import OperationalError, SQLAlchemyError
from sqlalchemy.orm import declarative_base, sessionmaker


# Configure logging for container environments.
logging.basicConfig(level=logging.INFO, format="%(asctime)s - %(levelname)s - %(message)s")
logger = logging.getLogger("bridge")
logger.setLevel(os.environ.get("LOG_LEVEL", "INFO").upper())


# Define the base declarative class for SQLAlchemy.
Base = declarative_base()


class VehicleTelemetry(Base):
    __tablename__ = "vehicle_telemetry"

    id = Column(Integer, primary_key=True, autoincrement=True)

    node_id = Column(String(64), nullable=False, index=True)
    car = Column(Integer, nullable=False)
    motorcycle = Column(Integer, nullable=False)
    rssi = Column(Integer, nullable=False)
    snr = Column(Float, nullable=False)

    timestamp = Column(DateTime(timezone=True), nullable=False, index=True)


def _utcnow() -> datetime:
    return datetime.now(timezone.utc)


# Input validation schema for edge telemetry.
class VehicleTelemetryIn(BaseModel):
    node_id: str = Field(..., min_length=1)
    car: int = Field(..., ge=0)
    motorcycle: int = Field(..., ge=0)
    # RSSI/SNR have default values for backward compatibility with dev payloads.
    rssi: int = 0
    snr: float = 0.0


class VehicleTelemetryOut(BaseModel):
    id: int
    node_id: str
    car: int
    motorcycle: int
    rssi: int
    snr: float
    timestamp: datetime


# Enqueue incoming MQTT payloads for async database insertion.
class TelemetryBridge:
    def __init__(self) -> None:
        self.database_url = os.environ.get(
            "DATABASE_URL",
            # Default to SQLite for local development (no container dependency).
            "sqlite:///./telemetry.db",
        )

        # Fallback to local machine broker for development.
        self.mqtt_broker = os.environ.get("MQTT_BROKER", "localhost")
        self.mqtt_port = int(os.environ.get("MQTT_PORT", "1883"))
        # Match the firmware default topics (node_b_gateway) and telegraf config.
        self.mqtt_topic = os.environ.get("MQTT_TOPIC", "traffic/counts")
        self.mqtt_client_id = os.environ.get(
            "MQTT_CLIENT_ID", f"vehicle-bridge-{os.getpid()}"
        )

        self.mqtt_username = os.environ.get("MQTT_USERNAME")
        self.mqtt_password = os.environ.get("MQTT_PASSWORD")

        # TLS is required for many managed brokers (e.g., HiveMQ Cloud).
        # Enable explicitly via MQTT_TLS=1, or implicitly when port is 8883.
        self.mqtt_tls = (
            os.environ.get("MQTT_TLS", "").strip().lower() in {"1", "true", "yes"}
            or self.mqtt_port == 8883
        )
        self.mqtt_tls_insecure = (
            os.environ.get("MQTT_TLS_INSECURE", "").strip().lower() in {"1", "true", "yes"}
        )
        self.mqtt_tls_ca_certs = os.environ.get("MQTT_TLS_CA_CERTS")

        # Database disconnection buffer. Maintains queue during short DB downtimes.
        self._queue: queue.Queue[tuple[VehicleTelemetryIn, int]] = queue.Queue(maxsize=1000)

        self._stop_event = threading.Event()
        self._worker_thread: Optional[threading.Thread] = None

        engine_kwargs: dict[str, Any] = {"future": True}
        if self.database_url.startswith("sqlite"):
            # Allows DB access from the MQTT worker thread.
            engine_kwargs["connect_args"] = {"check_same_thread": False}
        else:
            engine_kwargs["pool_pre_ping"] = True
            engine_kwargs["pool_size"] = int(os.environ.get("DB_POOL_SIZE", "5"))
            engine_kwargs["max_overflow"] = int(os.environ.get("DB_MAX_OVERFLOW", "10"))

        self._engine = create_engine(self.database_url, **engine_kwargs)
        self._SessionLocal = sessionmaker(bind=self._engine, autoflush=False, autocommit=False)

        # paho-mqtt 2.x changed callback signatures; pin to the v1 callback API so the
        # same code works across environments.
        self._mqtt = self._create_mqtt_client()
        if self.mqtt_username:
            self._mqtt.username_pw_set(self.mqtt_username, self.mqtt_password)

        self._mqtt.on_connect = self._on_connect
        self._mqtt.on_disconnect = self._on_disconnect
        self._mqtt.on_message = self._on_message

        # Built-in reconnect backoff (seconds)
        self._mqtt.reconnect_delay_set(min_delay=1, max_delay=30)

        try:
            self._mqtt.enable_logger(logger)
        except Exception:
            # Older paho versions can be fussy about logger config; no big deal.
            pass

    def _create_mqtt_client(self) -> mqtt.Client:
        try:
            if hasattr(mqtt, "CallbackAPIVersion"):
                return mqtt.Client(
                    client_id=self.mqtt_client_id,
                    clean_session=True,
                    protocol=mqtt.MQTTv311,
                    callback_api_version=mqtt.CallbackAPIVersion.VERSION1,
                )
        except TypeError:
            # Older/newer constructor signatures; fall back.
            pass

        return mqtt.Client(
            client_id=self.mqtt_client_id,
            clean_session=True,
            protocol=mqtt.MQTTv311,
        )

    # -----------------------------
    # Lifecycle
    # -----------------------------
    def start(self) -> None:
        logger.info(
            "Starting telemetry bridge (MQTT %s:%s topic=%s, DB=%s)",
            self.mqtt_broker,
            self.mqtt_port,
            self.mqtt_topic,
            self._redact_db_url(self.database_url),
        )

        self._stop_event.clear()

        # Start the DB worker first so we can buffer immediately.
        self._worker_thread = threading.Thread(
            target=self._db_worker,
            name="telemetry-db-worker",
            daemon=True,
        )
        self._worker_thread.start()

        # Connect asynchronously so app startup doesn't block on broker availability.
        try:
            if self.mqtt_tls:
                # With ca_certs=None, paho uses system default CA bundle.
                self._mqtt.tls_set(
                    ca_certs=self.mqtt_tls_ca_certs or None,
                    cert_reqs=ssl.CERT_REQUIRED,
                    tls_version=ssl.PROTOCOL_TLS_CLIENT,
                )
                if self.mqtt_tls_insecure:
                    self._mqtt.tls_insecure_set(True)

            self._mqtt.connect_async(self.mqtt_broker, self.mqtt_port, keepalive=60)
            self._mqtt.loop_start()
        except Exception:
            logger.exception("Failed to start MQTT client")

    def stop(self) -> None:
        logger.info("Stopping telemetry bridge")
        self._stop_event.set()

        try:
            self._mqtt.loop_stop()
        except Exception:
            logger.exception("Error stopping MQTT loop")

        try:
            self._mqtt.disconnect()
        except Exception:
            logger.exception("Error disconnecting MQTT")

        if self._worker_thread and self._worker_thread.is_alive():
            self._worker_thread.join(timeout=5)

        try:
            self._engine.dispose()
        except Exception:
            logger.exception("Error disposing DB engine")

    # -----------------------------
    # MQTT callbacks
    # -----------------------------
    def _on_connect(self, client: mqtt.Client, userdata: Any, flags: dict[str, Any], rc: int) -> None:
        if rc == 0:
            logger.info("MQTT connected; subscribing to %s", self.mqtt_topic)
            try:
                client.subscribe(self.mqtt_topic, qos=0)
            except Exception:
                logger.exception("MQTT subscribe failed")
        else:
            logger.warning("MQTT connect failed rc=%s", rc)

    def _on_disconnect(self, client: mqtt.Client, userdata: Any, rc: int) -> None:
        if self._stop_event.is_set():
            return
        logger.warning("MQTT disconnected rc=%s (will auto-reconnect)", rc)

    def _on_message(self, client: mqtt.Client, userdata: Any, msg: mqtt.MQTTMessage) -> None:
        try:
            raw = msg.payload.decode("utf-8", errors="replace")
            print(f"🔥 [FASTAPI MQTT] Received on {msg.topic}: {raw}", flush=True)
            data = json.loads(raw)

            # Firmware payloads commonly use "node"; normalize to backend's "node_id".
            if isinstance(data, dict) and "node_id" not in data and "node" in data:
                data["node_id"] = data.get("node")

            if hasattr(VehicleTelemetryIn, "model_validate"):
                payload = VehicleTelemetryIn.model_validate(data)  # type: ignore[attr-defined]
            else:
                payload = VehicleTelemetryIn.parse_obj(data)  # type: ignore[attr-defined]
        except Exception:
            logger.exception("Invalid telemetry payload on %s", msg.topic)
            return

        try:
            self._queue.put_nowait((payload, 0))
        except queue.Full:
            # Avoid blocking the MQTT thread. If we're overloaded, dropping is better than
            # building up latency and eventually getting kicked by the broker.
            logger.warning("Telemetry queue full; dropping message")

    # -----------------------------
    # DB worker
    # -----------------------------
    def _ensure_tables_with_retry(self) -> None:
        backoff = 1.0
        while not self._stop_event.is_set():
            try:
                Base.metadata.create_all(self._engine)
                logger.info("DB ready; ensured tables exist")
                return
            except OperationalError:
                logger.warning("DB not ready yet; retrying in %.1fs", backoff)
            except SQLAlchemyError:
                logger.exception("DB error while creating tables; retrying in %.1fs", backoff)

            time.sleep(backoff)
            backoff = min(backoff * 1.8, 30.0)

    def _db_worker(self) -> None:
        self._ensure_tables_with_retry()

        max_retries = int(os.environ.get("DB_WRITE_MAX_RETRIES", "5"))
        retry_backoff_s = float(os.environ.get("DB_WRITE_RETRY_BACKOFF_S", "1.0"))

        while not self._stop_event.is_set():
            try:
                payload, retries = self._queue.get(timeout=0.5)
            except queue.Empty:
                continue

            try:
                self._write_row(payload)
            except OperationalError:
                # When Postgres drops connections (restart, network blip), retry a few
                # times. Past that, we drop to keep the system responsive.
                if retries < max_retries and not self._stop_event.is_set():
                    logger.warning(
                        "DB unavailable; requeueing telemetry (retry %s/%s)",
                        retries + 1,
                        max_retries,
                    )
                    self._ensure_tables_with_retry()
                    time.sleep(retry_backoff_s)
                    try:
                        self._queue.put_nowait((payload, retries + 1))
                    except queue.Full:
                        logger.warning("Telemetry queue full during retry; dropping")
                else:
                    logger.warning("DB unavailable; dropping telemetry after retries")
            except SQLAlchemyError:
                # Non-transient DB error (e.g., schema issue): log and drop.
                logger.exception("DB error while persisting telemetry; dropping")
            except Exception:
                # Never let the worker die; log and continue.
                logger.exception("Unexpected error persisting telemetry; dropping")
            finally:
                self._queue.task_done()

    def _write_row(self, payload: VehicleTelemetryIn) -> None:
        row = VehicleTelemetry(
            node_id=payload.node_id,
            car=payload.car,
            motorcycle=payload.motorcycle,
            rssi=payload.rssi,
            snr=payload.snr,
            timestamp=_utcnow(),
        )

        try:
            with self._SessionLocal() as session:
                session.add(row)
                session.commit()
        except OperationalError:
            # If the pool contains dead connections, dispose forces a clean reconnect.
            logger.warning("DB operational error; disposing engine pool")
            try:
                self._engine.dispose()
            except Exception:
                logger.exception("Failed to dispose engine")
            raise
        except SQLAlchemyError:
            raise

    @staticmethod
    def _redact_db_url(url: str) -> str:
        # Very small helper to avoid logging passwords.
        try:
            if "@" not in url or "://" not in url:
                return url
            scheme, rest = url.split("://", 1)
            creds, hostpart = rest.split("@", 1)
            if ":" in creds:
                user = creds.split(":", 1)[0]
                return f"{scheme}://{user}:***@{hostpart}"
        except Exception:
            return "<redacted>"
        return url


bridge = TelemetryBridge()


@asynccontextmanager
async def lifespan(app: FastAPI):
    bridge.start()
    try:
        yield
    finally:
        bridge.stop()


app = FastAPI(title="Vehicle Telemetry Bridge", lifespan=lifespan)

api_key_header = APIKeyHeader(name="X-API-Key", auto_error=False)

def get_api_key(api_key: str = Depends(api_key_header)):
    expected_key = os.environ.get("API_KEY", "local-dev-api-key")
    if not api_key or api_key != expected_key:
        raise HTTPException(status_code=403, detail="Forbidden")
    return api_key

        # Allow CORS for Next.js browser client. Update allowed domains before deployment.
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=False,
    allow_methods=["*"],
    allow_headers=["*"]
)


@app.get("/health")
def health() -> dict[str, str]:
    return {"status": "ok"}


@app.get("/api/telemetry", response_model=list[VehicleTelemetryOut])
def get_last_telemetry(
    start_time: Optional[datetime] = None,
    end_time: Optional[datetime] = None,
    api_key: str = Depends(get_api_key),
) -> list[VehicleTelemetryOut]:
    # Without a time filter we keep this endpoint cheap and predictable (last 20).
    # With a time filter we return the range so the UI can chart it.
    try:
        with bridge._SessionLocal() as session:
            query = session.query(VehicleTelemetry)

            if start_time is not None:
                query = query.filter(VehicleTelemetry.timestamp >= _coerce_dt(start_time))
            if end_time is not None:
                query = query.filter(VehicleTelemetry.timestamp <= _coerce_dt(end_time))

            if start_time is None and end_time is None:
                rows = query.order_by(VehicleTelemetry.timestamp.desc()).limit(20).all()
            else:
                rows = query.order_by(VehicleTelemetry.timestamp.asc()).all()
    except OperationalError:
        raise HTTPException(status_code=503, detail="Database unavailable")
    except SQLAlchemyError:
        logger.exception("DB query failed")
        raise HTTPException(status_code=500, detail="Database error")

    return [
        VehicleTelemetryOut(
            id=r.id,
            node_id=r.node_id,
            car=r.car,
            motorcycle=r.motorcycle,
            rssi=r.rssi,
            snr=float(r.snr),
            timestamp=r.timestamp,
        )
        for r in rows
    ]


def _coerce_dt(dt: datetime) -> datetime:
    # If a client sends a naive datetime, treat it as UTC rather than guessing local time.
    if dt.tzinfo is None:
        return dt.replace(tzinfo=timezone.utc)
    return dt
