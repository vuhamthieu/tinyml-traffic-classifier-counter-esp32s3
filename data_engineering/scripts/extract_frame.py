import cv2
import os

VIDEO_PATH = 'data_engineering/raw_videos/traffic_video.mp4'
OUTPUT_DIR = 'data_engineering/dataset'

FRAME_INTERVAL = 30    
MAX_IMAGES = 150        
TARGET_SIZE = 320       

def extract_frames():
    if not os.path.exists(OUTPUT_DIR):
        os.makedirs(OUTPUT_DIR)
        print(f"-> Folder created: {OUTPUT_DIR}")

    if not os.path.exists(VIDEO_PATH):
        print(f"ERROR: Video not found at: {VIDEO_PATH}")
        print("Download video to 'data_engineering/raw_videos/'.")
        return

    cap = cv2.VideoCapture(VIDEO_PATH)
    total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    print(f"-> Processing Video... (Total: {total_frames} frames)")

    count = 0
    saved_count = 0

    while cap.isOpened() and saved_count < MAX_IMAGES:
        ret, frame = cap.read()
        if not ret:
            breakVideo

        if count % FRAME_INTERVAL == 0:
            h, w, _ = frame.shape
            
            min_dim = min(h, w)
            start_x = (w - min_dim) // 2
            start_y = (h - min_dim) // 2
            cropped = frame[start_y : start_y + min_dim, start_x : start_x + min_dim]

            final_img = cv2.resize(cropped, (TARGET_SIZE, TARGET_SIZE))

            filename = os.path.join(OUTPUT_DIR, f"data_{saved_count:03d}.jpg")
            cv2.imwrite(filename, final_img)
            
            print(f"Saved: {filename}", end='\r')
            saved_count += 1
        
        count += 1

    cap.release()
    print(f"\n\n===DONE===")
    print(f"Saved {saved_count} to '{OUTPUT_DIR}'")

if __name__ == "__main__":
    extract_frames()