﻿//g++ yoloX.cpp -o yoloX `pkg-config --cflags --libs opencv4`

/*When doing inference, the preprocessing did not do BGR2RGB,
divided by 255.0, minus the mean and divided by the variance. 
Therefore, if you generate an onnx file after training with the latest code, and then use the program in this repository for inference, 
you need to comment out "BGR2RGB, divided by 255.0, minus the mean and divided by the variance These steps" */

#include <fstream>
#include <sstream>
#include <iostream>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>

using namespace cv;
using namespace dnn;
using namespace std;
/************************************************************************************************************/
class yolox
{
public:
    yolox(string modelpath, float confThreshold, float nmsThreshold,bool is_cuda);
    void detect(Mat& srcimg);

private:
    const int stride[3] = { 8, 16, 32 };
    const string classesFile = "config_files/coco.names";   ////This is the class name for storing the COCO dataset. If you train with your own dataset, you need to modify it
    const int input_shape[2] = { 640, 640 };   //// height, width
    const float mean[3] = { 0.485, 0.456, 0.406 };
    const float std[3] = { 0.229, 0.224, 0.225 };
    float prob_threshold;
    float nms_threshold;
    vector<string> classes;
    int num_class;
    Net net;

    Mat resize_image(Mat srcimg, float* scale);
    void normalize(Mat& srcimg);
    int get_max_class(float* scores);
};
/************************************************************************************************************/
yolox::yolox(string modelpath, float confThreshold, float nmsThreshold,bool is_cuda)
{
    this->prob_threshold = confThreshold;
    this->nms_threshold = nmsThreshold;

    ifstream ifs(this->classesFile.c_str());
    string line;
    while (getline(ifs, line)) this->classes.push_back(line);
    this->num_class = this->classes.size();
    this->net = readNet(modelpath);
    if (is_cuda)
    {
        std::cout << "Using CUDA\n";
        this->net.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
        this->net.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA_FP16);
    }
    else
    {
        std::cout << "Running on CPU\n";
        this->net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        this->net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    }
}
/************************************************************************************************************/
Mat yolox::resize_image(Mat srcimg, float* scale)
{
    float r = std::min(this->input_shape[1] / (srcimg.cols*1.0), this->input_shape[0] / (srcimg.rows*1.0));
    *scale = r;
    // r = std::min(r, 1.0f);
    int unpad_w = r * srcimg.cols;
    int unpad_h = r * srcimg.rows;
    Mat re(unpad_h, unpad_w, CV_8UC3);
    resize(srcimg, re, re.size());
    Mat out(this->input_shape[1], this->input_shape[0], CV_8UC3, Scalar(114, 114, 114));
    re.copyTo(out(Rect(0, 0, re.cols, re.rows)));
    return out;
}
/************************************************************************************************************/
void yolox::normalize(Mat& img)
{
    cvtColor(img, img, cv::COLOR_BGR2RGB);
    img.convertTo(img, CV_32F);
    int i = 0, j = 0;
    for (i = 0; i < img.rows; i++)
    {
        float* pdata = (float*)(img.data + i * img.step);
        for (j = 0; j < img.cols; j++)
        {
            pdata[0] = (pdata[0] / 255.0 - this->mean[0]) / this->std[0];
            pdata[1] = (pdata[1] / 255.0 - this->mean[1]) / this->std[1];
            pdata[2] = (pdata[2] / 255.0 - this->mean[2]) / this->std[2];
            pdata += 3;
        }
    }
}
/***********************************************************************************************************/
int yolox::get_max_class(float* scores)
{
    float max_class_socre = 0, class_socre = 0;
    int max_class_id = 0, c = 0;
    for (c = 0; c < this->num_class; c++) //// get max socre
    {
        if (scores[c] > max_class_socre)
        {
            max_class_socre = scores[c];
            max_class_id = c;
        }
    }
    return max_class_id;
}
/************************************************************************************************************/
void yolox::detect(Mat& srcimg)
{
    float scale = 1.0;
    Mat dstimg = this->resize_image(srcimg, &scale);
    //this->normalize(dstimg);
    Mat blob = blobFromImage(dstimg);

    this->net.setInput(blob);
    vector<Mat> outs;
    this->net.forward(outs, this->net.getUnconnectedOutLayersNames());
    if (outs[0].dims == 3)
    {
        const int num_proposal = outs[0].size[1];
        outs[0] = outs[0].reshape(0, num_proposal);
    }
    /////generate proposals, decode outputs
    vector<int> classIds;
    vector<float> confidences;
    vector<Rect> boxes;
    float ratioh = (float)srcimg.rows / this->input_shape[0], ratiow = (float)srcimg.cols / this->input_shape[1];
    int n = 0, i = 0, j = 0, nout = this->classes.size() + 5, row_ind = 0;
    float* pdata = (float*)outs[0].data;
    for (n = 0; n < 3; n++)
    {
        const int num_grid_x = (int)(this->input_shape[1] / this->stride[n]);
        const int num_grid_y = (int)(this->input_shape[0] / this->stride[n]);
        for (i = 0; i < num_grid_y; i++)
        {
            for (j = 0; j < num_grid_x; j++)
            {
                float box_score = pdata[4];
//                for (int class_idx = 0; class_idx < this->num_class; class_idx++)
//                {
//                    float cls_score = pdata[5 + class_idx];
//                    float box_prob = box_score * cls_score;
//                    if (box_prob > this->prob_threshold)
//                    {
//                        float x_center = (pdata[0] + j) * this->stride[n];
//                        float y_center = (pdata[1] + i) * this->stride[n];
//                        float w = exp(pdata[2]) * this->stride[n];
//                        float h = exp(pdata[3]) * this->stride[n];
//                        float x0 = x_center - w * 0.5f;
//                        float y0 = y_center - h * 0.5f;

//                        classIds.push_back(class_idx);
//                        confidences.push_back(box_prob);
//                        boxes.push_back(Rect(int(x0), int(y0), (int)(w), (int)(h)));
//                    }
//                }

                //int class_idx = this->get_max_class(pdata + 5);
                Mat scores = outs[0].row(row_ind).colRange(5, outs[0].cols);
                Point classIdPoint;
                double max_class_socre;
                // Get the value and location of the maximum score
                minMaxLoc(scores, 0, &max_class_socre, 0, &classIdPoint);
                int class_idx = classIdPoint.x;

                float cls_score = pdata[5 + class_idx];
                float box_prob = box_score * cls_score;
                if (box_prob > this->prob_threshold)
                {
                    float x_center = (pdata[0] + j) * this->stride[n];
                    float y_center = (pdata[1] + i) * this->stride[n];
                    float w = exp(pdata[2]) * this->stride[n];
                    float h = exp(pdata[3]) * this->stride[n];
                    float x0 = x_center - w * 0.5f;
                    float y0 = y_center - h * 0.5f;

                    classIds.push_back(class_idx);
                    confidences.push_back(box_prob);
                    boxes.push_back(Rect(int(x0), int(y0), (int)(w), (int)(h)));
                }

                pdata += nout;
                row_ind++;
            }
        }
    }

    // Perform non maximum suppression to eliminate redundant overlapping boxes with
    // lower confidences
    vector<int> indices;
    NMSBoxes(boxes, confidences, this->prob_threshold, this->nms_threshold, indices);
    for (size_t i = 0; i < indices.size(); ++i)
    {
        int idx = indices[i];
        Rect box = boxes[idx];
        // adjust offset to original unpadded
        float x0 = (box.x) / scale;
        float y0 = (box.y) / scale;
        float x1 = (box.x + box.width) / scale;
        float y1 = (box.y + box.height) / scale;

        // clip
        x0 = std::max(std::min(x0, (float)(srcimg.cols - 1)), 0.f);
        y0 = std::max(std::min(y0, (float)(srcimg.rows - 1)), 0.f);
        x1 = std::max(std::min(x1, (float)(srcimg.cols - 1)), 0.f);
        y1 = std::max(std::min(y1, (float)(srcimg.rows - 1)), 0.f);

        rectangle(srcimg, Point(x0, y0), Point(x1, y1), Scalar(0, 0, 255), 2);
        //Get the label for the class name and its confidence
        string label = format("%.2f", confidences[idx]);
        label = this->classes[classIds[idx]] + ":" + label;
        //Display the label at the top of the bounding box
        int baseLine;
        Size labelSize = getTextSize(label, FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);
        y0 = std::max(y0, (float)labelSize.height);
        //rectangle(frame, Point(left, top - int(1.5 * labelSize.height)), Point(left + int(1.5 * labelSize.width), top + baseLine), Scalar(0, 255, 0), FILLED);
        putText(srcimg, label, Point(x0, y0), FONT_HERSHEY_SIMPLEX, 0.75, Scalar(0, 255, 0), 1);
    }
}
/****************************************************************************************************************/
int main(int argc, char **argv)
{
    std::cout << "The current OpenCV version is " << CV_VERSION << "\n";
    //cv::VideoCapture capture("sample1.mp4",cv::CAP_FFMPEG);
    const std::string RTSP_URL = "rtsp://service:service@172.196.129.152:554/ufirststream?inst=2"; 
    //const std::string RTSP_URL = "rtsp://172.196.128.151:554/1/h264minor"; 
    setenv("OPENCV_FFMPEG_CAPTURE_OPTIONS", "rtsp_transport;tcp", 1);
    cv::VideoCapture capture(RTSP_URL,cv::CAP_FFMPEG); //cv::CAP_FFMPEG
    if(!capture.isOpened())
    {
        std::cerr << "Error opening video file\n";
        return -1;
    }
    bool is_cuda = argc > 1 && strcmp(argv[1], "cuda") == 0;
    yolox model("config_files/yolox_darknet.onnx", 0.6, 0.6, is_cuda); //yolox_s.onnx

    Mat frame;

    while(capture.isOpened())
    {
        bool OK = capture.grab();
        if (OK == false)
        {
            std::cout << "cannot grab" <<"\n";
        }
        else
        {
            //retrieve a frame of your source
            capture.read(frame);
        }
        if(frame.empty())
        {
            std::cout << "End of stream\n";
            break;
        }
        else
        {
            model.detect(frame);
            cv::imshow("YOLOX ObjectDetection using OpenCV", frame);
            if (cv::waitKey(1) != -1)
            {
                capture.release();
                destroyAllWindows();
                std::cout << "finished by user\n";
                break;
            }
        }
    }
}
/************************************************************************************************************/

