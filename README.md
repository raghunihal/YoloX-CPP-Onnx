# YoloX-CPP-Onnx
YOLOX-X is an objects detection model. 
YOLO models are some of the most widely used detectors in the industry due to their wide compatibility. 
Equipped with some of the latest advanced detection techniques, YOLO-X achieves the best compromise between speed and accuracy compared to other YOLO series models.

# Model
Add the model yolox_darknet.onnx in config_files folder 


# Compilation Command
g++ yoloX.cpp -o yoloX `pkg-config --cflags --libs opencv4`

