#include "image.hpp"

#include <iostream>

namespace imagenet {

RawImage::RawImage(std::string path) {
  std::ifstream file(path, std::ios::binary);

  file.seekg(0, std::ios::end);
  size_t fileSize = file.tellg();
  file.seekg(0, std::ios::beg);

  data.resize(fileSize);
  file.read(data.data(), fileSize);
}

Image::Image(std::vector<char> vec) : data(std::move(vec)) {}

// copies cv::Mat to a byte vector for serialisation
Image::Image(cv::Mat mat) {
  if (mat.isContinuous()) {
    data.assign(mat.data, mat.data + mat.total() * mat.channels());
  } else {
    for (int i = 0; i < mat.rows; ++i) {
      data.insert(data.end(), mat.ptr<char>(i),
                  mat.ptr<char>(i) + mat.cols * mat.channels());
    }
  }
}

Image kernel(RawImage image) {
  cv::Mat raw_img(1, image.data.size(), CV_8UC1, image.data.data());
  cv::Mat img = cv::imdecode(raw_img, cv::IMREAD_COLOR);
  if (img.data == NULL) {
    std::cout << "Decode image error!" << std::endl;
    exit(-1);
  }

  cv::Rect roi(0, 0, std::min(img.cols, 224), std::min(img.rows, 224));
  img = img(roi);
  cv::flip(img, img, 0);

  cv::Point2f pc(img.cols / 2., img.rows / 2.);
  cv::Mat r = cv::getRotationMatrix2D(pc, 20, 1.0);
  cv::warpAffine(img, img, r, img.size());

  cv::normalize(img, img, 0, 1, cv::NORM_MINMAX, CV_32F);

  return Image(img);
}

}  // namespace imagenet
