// #include <arm_neon.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class ScopeTime {
 public:
  ScopeTime() : m_begin(std::chrono::high_resolution_clock::now()) {}
  void LogTime() const {
    auto t = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - m_begin);
    float elapsed = (float)(t.count() * 1.0) / 1000.0;
    std::cerr << elapsed << "s\n";
  }

 private:
  std::chrono::time_point<std::chrono::high_resolution_clock> m_begin;
};

struct Matrix {
  typedef std::vector<int32_t> Mat1D;
  typedef std::vector<std::vector<int32_t>> Mat2D;
};
std::ostream &operator<<(std::ostream &os, const Matrix::Mat1D &mat) {
  os << "{";
  for (int i = 0; i < mat.size(); ++i) {
    if (i != 0) os << ",";
    os << mat[i];
  }
  os << "}";
  return os;
}
std::ostream &operator<<(std::ostream &os, const Matrix::Mat2D &mat) {
  os << "[";
  for (auto &it : mat) {
    os << it;
  }
  os << "]";
  return os;
}

class Logistics {
 public:
  Logistics(const std::string &train, const std::string &predict,
            const std::string &result, const std::string &answer)
      : m_trainFile(train),
        m_predictFile(predict),
        m_resultFile(result),
        m_answerFile(answer) {}

  void InitData();
  void Train();
  void Predict();
  void WriteAnswer();
  void Score();

 public:
  //  6400 912 79%
  int m_samples = 5980;
  int m_features = 1000;
  const int NTHREAD = 12;  // 线程个数

 private:
  Matrix::Mat1D m_MeanLabel[2];
  int m_CountLabel[2];

  Matrix::Mat1D m_PredictSum;
  Matrix::Mat1D m_PredictDelta;
  Matrix::Mat1D m_Answer;

  Matrix::Mat1D m_Indexs;

 private:
  std::string m_trainFile;
  std::string m_predictFile;
  std::string m_resultFile;
  std::string m_answerFile;
};

void Logistics::InitData() {
  m_MeanLabel[0] = Matrix::Mat1D(m_features, 0);
  m_MeanLabel[1] = Matrix::Mat1D(m_features, 0);
  m_CountLabel[0] = 0;
  m_CountLabel[1] = 0;

  m_PredictSum = Matrix::Mat1D(20000, 0);
  m_PredictDelta = Matrix::Mat1D(20000, 0);
  m_Answer = Matrix::Mat1D(20000, 0);
}

void Logistics::Train() {
  ScopeTime t;
  // 读文件
  int fd = open(m_trainFile.c_str(), O_RDONLY);
  long long bufsize = m_samples * 6500;
  char *buffer = (char *)mmap(NULL, bufsize, PROT_READ, MAP_PRIVATE, fd, 0);

  // 线程变量
  std::vector<Matrix::Mat2D> threadSum(
      NTHREAD, Matrix::Mat2D(2, Matrix::Mat1D(m_features, 0)));
  Matrix::Mat2D threadCount(NTHREAD, Matrix::Mat1D(2, 0));

  auto foo = [&](int pid, long long start, long long end) {
    Matrix::Mat1D features(m_features);
    int idx = 0;
    const char *ptr = buffer + start;
    while (start < end) {
      int x = 1;
      if (ptr[0] == '-') {
        ++ptr;
        ++start;
        x = -1;
      }
      int num = (ptr[2] - '0') * 100 + (ptr[3] - '0') * 10;
      num *= x;
      features[idx++] = num;
      ptr += 6;
      start += 6;

      if (idx == m_features) {
        while (ptr[1] != '\n') ++ptr, ++start;
        int label = ptr[0] - '0';
        ++threadCount[pid][label];
        for (int i = 0; i < m_features; i += 4) {
          threadSum[pid][label][i] += features[i];
          threadSum[pid][label][i + 1] += features[i + 1];
          threadSum[pid][label][i + 2] += features[i + 2];
          threadSum[pid][label][i + 3] += features[i + 3];
        }
        idx = 0;
        ptr += 2;
        start += 2;
      }
    }
  };

  long long block = bufsize / (NTHREAD + 1);
  long long start = 0, end = 0;
  std::vector<std::thread> Threads(NTHREAD);

  for (int i = 0; i < NTHREAD; ++i) {
    end = start + block;
    while (buffer[end] != '\n') ++end;
    ++end;
    Threads[i] = std::thread(foo, i, start, end);
    start = end;
  }
  for (auto &it : Threads) it.join();

  for (int i = 0; i < NTHREAD; ++i) {
    for (int j = 0; j < m_features; ++j) {
      m_MeanLabel[0][j] += threadSum[i][0][j];
      m_MeanLabel[1][j] += threadSum[i][1][j];
    }
    m_CountLabel[0] += threadCount[i][0];
    m_CountLabel[1] += threadCount[i][1];
  }
  int totalCount = m_CountLabel[0] + m_CountLabel[1];

  const char *ptr = buffer + end;
  int idx = 0;
  Matrix::Mat1D features(m_features);
  for (; totalCount < m_samples;) {
    int x = 1;
    if (ptr[0] == '-') {
      ++ptr;
      x = -1;
    }
    int num = (ptr[2] - '0') * 100 + (ptr[3] - '0') * 10;
    num *= x;
    features[idx++] = num;
    ptr += 6;

    if (idx == m_features) {
      while (ptr[1] != '\n') ++ptr;
      int label = ptr[0] - '0';
      ++m_CountLabel[label];
      for (int i = 0; i < m_features; i += 4) {
        m_MeanLabel[label][i] += features[i];
        m_MeanLabel[label][i + 1] += features[i + 1];
        m_MeanLabel[label][i + 2] += features[i + 2];
        m_MeanLabel[label][i + 3] += features[i + 3];
      }
      idx = 0;
      ptr += 2;
      ++totalCount;
    }
  }

  for (int i = 0; i < m_features; ++i) {
    m_MeanLabel[0][i] /= m_CountLabel[0];
    m_MeanLabel[1][i] /= m_CountLabel[1];
    m_PredictSum[i] = (m_MeanLabel[1][i] + m_MeanLabel[0][i]);
    m_PredictDelta[i] = (m_MeanLabel[0][i] - m_MeanLabel[1][i]);
  }

  // std::cerr << m_CountLabel[0] << ", " << m_CountLabel[1] << "\n";
  // std::cerr << m_MeanLabel[0] << "\n";
  std::cerr << "@ train: ";
  t.LogTime();
}

void Logistics::Predict() {
  ScopeTime t;
  // 读文件
  struct stat sb;
  int fd = open(m_predictFile.c_str(), O_RDONLY);
  fstat(fd, &sb);
  char *buffer = (char *)mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

  int linesize = 6000;
  long long linenum = sb.st_size / linesize;

  // 子线程
  auto foo = [&](int startline, int endline) {
    long long move = startline * linesize;
    for (int line = startline; line < endline; ++line, move += linesize) {
      int distance = 0;
      for (int i = 0, pos = 0; i < m_features; pos += 60) {
        const char *ptr = &buffer[move + pos];
        for (int k = 0; k < 60; k += 6, ++i) {
          int num = (ptr[k + 2] - '0') * 200 + (ptr[k + 3] - '0') * 20;
          distance += (num - m_PredictSum[i]) * m_PredictDelta[i];
        }
      }
      m_Answer[line] = (distance < 0 ? 1 : 0);
    }
  };

  // 创建线程
  int start = 0, block = linenum / NTHREAD;
  std::vector<std::thread> Threads(NTHREAD);
  for (int i = 0; i < NTHREAD; ++i) {
    long long end = (i == NTHREAD - 1 ? linenum : start + block);
    Threads[i] = std::thread(foo, start, end);
    start += block;
  }
  for (auto &it : Threads) it.join();

  std::cerr << "@ predict: ";
  t.LogTime();
}

void Logistics::WriteAnswer() {
  FILE *fp = fopen(m_resultFile.c_str(), "w");
  for (auto &label : m_Answer) {
    char c[2];
    c[0] = label + '0';
    c[1] = '\n';
    fwrite(c, 2, 1, fp);
  }
  fclose(fp);
}
void Logistics::Score() {
  std::ifstream fin(m_answerFile);
  int x, index = 0;
  float ac = 0, tol = 0;
  while (fin >> x) {
    if (x == m_Answer[index++]) {
      ++ac;
    }
    ++tol;
  }
  fin.close();
  std::cerr << "@ accuracy: " << ac * 100 / tol << "%\n";
}

int main() {
  std::cerr << std::fixed << std::setprecision(3);
  std::ios_base::sync_with_stdio(false);
  std::cin.tie(nullptr);

  const std::string TRAIN = "/data/train_data.txt";
  const std::string PREDICT = "/data/test_data.txt";
  const std::string RESULT = "/projects/student/result.txt";
  const std::string ANSWER = "/projects/student/answer.txt";
  const std::string LOCAL_TRAIN = "../data/train_data.txt";
  const std::string LOCAL_PREDICT = "../data/test_data.txt";
  const std::string LOCAL_RESULT = "../data/result.txt";
  const std::string LOCAL_ANSWER = "../data/answer.txt";

#ifdef LOCAL
  Logistics lr(LOCAL_TRAIN, LOCAL_PREDICT, LOCAL_RESULT, LOCAL_ANSWER);
  lr.InitData();
  lr.Train();
  lr.Predict();
  lr.WriteAnswer();
  lr.Score();

#else
  Logistics lr(TRAIN, PREDICT, RESULT, ANSWER);
  lr.InitData();
  lr.Train();
  lr.Predict();
  lr.WriteAnswer();
#endif
  return 0;
}
