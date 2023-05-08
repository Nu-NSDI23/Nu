#include <malloc.h>

#include <iostream>
#include <opencv2/opencv.hpp>

cv::Mat f1() {
  cv::Mat img =
      cv::imread("/opt/kaiyan/imagenet/train_t3/n02085620/n02085620_4602.JPEG");
  cv::Rect roi(0, 0, 224, 224);
  img = img(roi);

  return img;
}

cv::Mat f2() {
  cv::Mat img =
      cv::imread("/opt/kaiyan/imagenet/train_t3/n02085620/n02085620_4602.JPEG");
  cv::Rect roi(0, 0, 224, 224);
  img = img(roi);
  cv::Mat dst = img.clone();
  return dst;
}

int main() {
  // f1() doesn't clone the image
  // cv::Mat m = f1();
  // f2() clones the image
  cv::Mat m = f2();
  // malloc_stats() observes that opencv frees memory in f2()
  malloc_stats();

  return 0;
}
