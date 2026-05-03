with open("/home/vuhamthieu/Projects/AIoT/tinyml-traffic-classifier-counter-esp32s3/backend/app/main.py", "r") as f:
    text = f.read()

# Add logging handler
old_log_setup = """# Keep this logger name stable so you can filter it in container logs.
logger = logging.getLogger("bridge")
logger.setLevel(os.environ.get("LOG_LEVEL", "INFO").upper())"""

new_log_setup = """# Keep this logger name stable so you can filter it in container logs.
logging.basicConfig(level=logging.INFO, format="%(asctime)s - %(levelname)s - %(message)s")
logger = logging.getLogger("bridge")
logger.setLevel(os.environ.get("LOG_LEVEL", "INFO").upper())"""

text = text.replace(old_log_setup, new_log_setup)

# Add explicit print to _on_message
old_on_msg = """    def _on_message(self, client: mqtt.Client, userdata: Any, msg: mqtt.MQTTMessage) -> None:
        try:
            raw = msg.payload.decode("utf-8", errors="replace")
            data = json.loads(raw)"""

new_on_msg = """    def _on_message(self, client: mqtt.Client, userdata: Any, msg: mqtt.MQTTMessage) -> None:
        try:
            raw = msg.payload.decode("utf-8", errors="replace")
            print(f"🔥 [FASTAPI MQTT] Received on {msg.topic}: {raw}", flush=True)
            data = json.loads(raw)"""

text = text.replace(old_on_msg, new_on_msg)

with open("/home/vuhamthieu/Projects/AIoT/tinyml-traffic-classifier-counter-esp32s3/backend/app/main.py", "w") as f:
    f.write(text)
