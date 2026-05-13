# YOLOv8n Object Detection on K230D Zero
# Optimized for 128MB LPDDR4 RAM
# Generated deployment script for CanMV

import sensor
import image
import time
import gc
from maix import nn, camera

# Configuration
MODEL_PATH = "/root/best_320_k230d.kmodel"
IMG_WIDTH = 320
IMG_HEIGHT = 320
CONFIDENCE_THRESHOLD = 0.25
IOU_THRESHOLD = 0.45
CLASS_NAMES = ['black', 'silver']

# Color palette for bounding boxes (BGR)
COLORS = [
    (255, 0, 0),    # Red
    (0, 255, 0),    # Green
    (0, 0, 255),    # Blue
    (255, 255, 0),  # Cyan
    (255, 0, 255),  # Magenta
    (0, 255, 255),  # Yellow
    (128, 0, 0),    # Dark Red
    (0, 128, 0),    # Dark Green
]


class YOLOv8Detector:
    def __init__(self, model_path, img_size=(IMG_WIDTH, IMG_HEIGHT)):
        """Initialize YOLOv8 detector for K230D"""
        print("Initializing YOLOv8 Detector...")
        
        self.img_width = img_size[0]
        self.img_height = img_size[1]
        
        # Load kmodel
        print(f"Loading model: {model_path}")
        try:
            self.kpu = nn.load(model_path)
            print("✓ Model loaded successfully")
        except Exception as e:
            print(f"✗ Failed to load model: {e}")
            raise
        
        # Initialize camera
        print("Initializing camera...")
        try:
            camera.camera_init()
            print("✓ Camera initialized")
        except Exception as e:
            print(f"✗ Failed to initialize camera: {e}")
            raise
        
        self.frame_count = 0
        self.fps = 0
        self.last_time = time.ticks_ms()
        
        # Memory management
        gc.collect()
        gc.threshold(gc.mem_free() // 4 + gc.mem_alloc())
    
    def preprocess(self, img):
        """Preprocess image for model input"""
        # Resize if needed (should match training size)
        if img.width() != self.img_width or img.height() != self.img_height:
            img = img.resize(self.img_width, self.img_height)
        
        # Convert to RGB (model expects RGB)
        # Note: CanMV images are already in RGB format
        return img
    
    def postprocess(self, outputs, conf_thresh=CONFIDENCE_THRESHOLD, iou_thresh=IOU_THRESHOLD):
        """
        Post-process YOLOv8 outputs
        Apply NMS and filter detections
        
        Returns: List of (x, y, w, h, conf, cls) tuples
        """
        detections = []
        
        # YOLOv8 output format: [batch, num_predictions, 4 + num_classes]
        # Where 4 = (x_center, y_center, width, height)
        
        try:
            # Get predictions (shape: [1, num_predictions, 4+nc])
            predictions = outputs[0]
            
            for pred in predictions:
                # Extract box coordinates and class scores
                x_center, y_center, width, height = pred[:4]
                class_scores = pred[4:]
                
                # Get max confidence and class
                max_conf = max(class_scores)
                if max_conf < conf_thresh:
                    continue
                
                class_id = class_scores.index(max_conf)
                
                # Convert to corner coordinates
                x1 = int((x_center - width / 2) * self.img_width)
                y1 = int((y_center - height / 2) * self.img_height)
                x2 = int((x_center + width / 2) * self.img_width)
                y2 = int((y_center + height / 2) * self.img_height)
                
                # Clamp to image bounds
                x1 = max(0, min(x1, self.img_width))
                y1 = max(0, min(y1, self.img_height))
                x2 = max(0, min(x2, self.img_width))
                y2 = max(0, min(y2, self.img_height))
                
                detections.append((x1, y1, x2-x1, y2-y1, max_conf, class_id))
        
        except Exception as e:
            print(f"Post-processing error: {e}")
        
        # Apply NMS (simple version for memory efficiency)
        detections = self.nms(detections, iou_thresh)
        
        return detections
    
    def nms(self, detections, iou_threshold):
        """Non-Maximum Suppression (memory-efficient)"""
        if len(detections) == 0:
            return []
        
        # Sort by confidence
        detections.sort(key=lambda x: x[4], reverse=True)
        
        keep = []
        while len(detections) > 0:
            # Keep highest confidence detection
            best = detections.pop(0)
            keep.append(best)
            
            # Remove overlapping detections
            detections = [
                det for det in detections
                if self.iou(best, det) < iou_threshold or best[5] != det[5]
            ]
        
        return keep
    
    def iou(self, box1, box2):
        """Calculate IoU between two boxes"""
        x1_1, y1_1, w1, h1 = box1[:4]
        x1_2, y1_2, w2, h2 = box2[:4]
        
        x2_1, y2_1 = x1_1 + w1, y1_1 + h1
        x2_2, y2_2 = x1_2 + w2, y1_2 + h2
        
        # Intersection
        xi1 = max(x1_1, x1_2)
        yi1 = max(y1_1, y1_2)
        xi2 = min(x2_1, x2_2)
        yi2 = min(y2_1, y2_2)
        
        inter_area = max(0, xi2 - xi1) * max(0, yi2 - yi1)
        
        # Union
        box1_area = w1 * h1
        box2_area = w2 * h2
        union_area = box1_area + box2_area - inter_area
        
        return inter_area / union_area if union_area > 0 else 0
    
    def draw_detections(self, img, detections):
        """Draw bounding boxes and labels on image"""
        for det in detections:
            x, y, w, h, conf, cls = det
            
            # Get color for this class
            color = COLORS[int(cls) % len(COLORS)]
            
            # Draw bounding box
            img.draw_rectangle(x, y, w, h, color=color, thickness=2)
            
            # Draw label
            label = f"{CLASS_NAMES[int(cls)]} {conf:.2f}"
            img.draw_string(x, y - 15, label, color=color, scale=1)
        
        return img
    
    def update_fps(self):
        """Calculate FPS"""
        self.frame_count += 1
        current_time = time.ticks_ms()
        
        if time.ticks_diff(current_time, self.last_time) >= 1000:
            self.fps = self.frame_count
            self.frame_count = 0
            self.last_time = current_time
    
    def run(self):
        """Main inference loop"""
        print("\nStarting inference...")
        print("Press Ctrl+C to stop\n")
        
        try:
            while True:
                # Capture frame
                img = camera.capture()
                if img is None:
                    continue
                
                # Preprocess
                input_img = self.preprocess(img)
                
                # Run inference
                outputs = self.kpu.run(input_img)
                
                # Post-process
                detections = self.postprocess(outputs)
                
                # Draw results
                img = self.draw_detections(img, detections)
                
                # Update FPS
                self.update_fps()
                
                # Draw FPS
                img.draw_string(10, 10, f"FPS: {self.fps}", color=(255, 255, 255), scale=2)
                
                # Display
                camera.display(img)
                
                # Memory management (critical for 128MB RAM)
                if self.frame_count % 10 == 0:
                    gc.collect()
        
        except KeyboardInterrupt:
            print("\nStopping inference...")
        finally:
            camera.camera_deinit()
            gc.collect()
            print("✓ Cleanup complete")


def main():
    print("=" * 60)
    print("YOLOv8n Object Detection on K230D Zero")
    print("=" * 60)
    print(f"Model: {MODEL_PATH}")
    print(f"Input Size: {IMG_WIDTH}x{IMG_HEIGHT}")
    print(f"Classes: {CLASS_NAMES}")
    print("=" * 60)
    
    # Initialize detector
    detector = YOLOv8Detector(MODEL_PATH)
    
    # Run inference
    detector.run()


if __name__ == "__main__":
    main()
