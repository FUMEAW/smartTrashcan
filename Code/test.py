from custom_detect import CustomTrashClassifier

model = CustomTrashClassifier("./best_ncnn_model")
model.launch_camera()

try:
    for detection in model.objects():
        print(detection)
        
        # Optional: Add a break condition if needed
except KeyboardInterrupt:
    print("Stream stopped by user.")
