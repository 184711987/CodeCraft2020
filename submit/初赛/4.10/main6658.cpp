#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <stack>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef LOCAL
#define TRAIN "../data/3512444/test_data.txt"
#define RESULT "../data/3512444/result.txt"
#else
#define TRAIN "/data/test_data.txt"
#define RESULT "/projects/student/result.txt"
#endif

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

std::ostream &operator<<(std::ostream &os, const std::vector<int> &mat) {
  os << "{";
  for (int i = 0; i < mat.size(); ++i) {
    if (i != 0) os << ",";
    os << mat[i];
  }
  os << "}";
  return os;
}
std::ostream &operator<<(std::ostream &os,
                         const std::vector<std::vector<int>> &mat) {
  os << "[";
  for (int i = 0; i < mat.size(); ++i) {
    if (i != 0) os << ",";
    os << mat[i];
  }
  os << "]";
  return os;
}

struct ThreadData {
  int answers = 0;                          // 环个数
  int start = 0;                            // 起点
  int startctg = 0;                         // sccID
  std::vector<int> tempPath;                // dfs临时路径
  std::vector<bool> OneReachable;           // 一步可达
  std::vector<bool> TwoReachable;           // 两步可达
  std::vector<bool> ThreeReachable;         // 三步可达
  std::vector<std::vector<int>> Answer[5];  // 结果
  std::vector<bool> vis;
  ThreadData(int n) {
    tempPath = std::vector<int>(7, -1);
    vis = std::vector<bool>(n, false);
  }
};

class XJBG {
 public:
  static const int NTHREAD = 4;        // 线程个数
  static const int LIMIT_STEP = 3;     // 预估步长
  static const int MAXN = 560000 + 7;  // 总点数
  const int PARAM[NTHREAD] = {2, 3, 5, 40};
  // const int PARAM[NTHREAD] = {1};

 private:
  std::unordered_map<int, int> m_IDToMap;  // ID->MapID
  std::vector<int> m_IDDom;                // MapID->ID
  int m_answers = 0;                       // 总环数目
  std::vector<int> m_Circles;              // 大于3的联通分量的点
  std::vector<ThreadData> ThreadsData;     // 线程数据

 private:
  std::vector<std::tuple<int, int>> m_Sons[MAXN];     // 边
  std::vector<std::tuple<int, int>> m_Fathers[MAXN];  // 边
  int m_edgeNum = 0;                                  // 边数目
  int m_dfn[MAXN], m_low[MAXN];                       // Tarjan
  std::stack<int> m_stack;                            // Tarjan
  int m_category[MAXN];                               // 所在联通分量id
  int m_inDegree[MAXN], m_outDegree[MAXN];            // 出入度
  bool m_inStack[MAXN];         // Tarjan标记在不在栈内
  bool m_inCircle[MAXN];        // 在不在找过的环内
  int m_tarjanCount = 0;        // Tarjan搜索顺序
  int m_stackTop = 0;           // Tarjan栈top标号
  int m_scc = 0, m_useScc = 0;  // 总联通分量和大于3的

 public:
  void LoadData();    // 加载数据
  void TarJan();      // TarJan
  void FindCircle();  // 找环
  void SaveAnswer();  // 保存答案

 private:
  inline int GetID(int x);                                     // 获取ID
  int GetMapID(int x);                                         // 获取映射ID
  inline void addEdge(int u, int v, int w);                    // 加边
  void doTarJan(int u);                                        // tarjan
  bool judge(ThreadData &Data, const int &u, const int &dep);  // dfs剪枝
  inline void SaveCircle(ThreadData &Data, const int &dep);    // 保存环
  void BackSearch(ThreadData &Data, int &st, int dep);         // 反向剪枝
  void doFindCircle(ThreadData &Data, int u, int dep);         // 正向找环
};

inline int XJBG::GetID(int x) { return m_IDDom[x]; }
int XJBG::GetMapID(int x) {
  auto it = m_IDToMap.find(x);
  if (it != m_IDToMap.end()) {
    return it->second;
  }
  int sz = m_IDDom.size();
  m_IDToMap.insert({x, sz});
  m_IDDom.emplace_back(x);
  return sz;
}
inline void XJBG::addEdge(int u, int v, int w) {
  u = this->GetMapID(u);
  v = this->GetMapID(v);
  m_Sons[u].emplace_back(std::make_tuple(v, w));
  m_Fathers[v].emplace_back(std::make_tuple(u, w));
  ++m_edgeNum;
  ++m_inDegree[v];
  ++m_outDegree[u];
}

void XJBG::LoadData() {
  ScopeTime t;

  struct stat sb;
  int fd = open(TRAIN, O_RDONLY);
  fstat(fd, &sb);
  long long bufsize = sb.st_size;
  char *buffer = (char *)mmap(NULL, bufsize, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);

  int u = 0, v = 0, w = 0;
  char *ptr = buffer;
  while (ptr - buffer < bufsize) {
    while (*ptr != ',') {
      u = u * 10 + *ptr - '0';
      ++ptr;
    }
    ++ptr;
    while (*ptr != ',') {
      v = v * 10 + *ptr - '0';
      ++ptr;
    }
    ++ptr;
    while (*ptr != '\n') {
      w = w * 10 + *ptr - '0';
      ++ptr;
    }
    ++ptr;
    addEdge(u, v, w);
    u = v = w = 0;
  }

#ifdef LOCAL
  std::cerr << "@ LoadData: (V: " << m_IDDom.size() << ", E: " << m_edgeNum
            << ") #";
  t.LogTime();
#endif
#ifdef TEST
  std::set<std::vector<int>> st;
  for (int i = 0; i < MAXN; ++i) {
    for (auto &it : m_Sons[i]) {
      st.insert({i, it.v});
    }
  }
  std::cerr << "@ unique edge: " << st.size() << "\n";
#endif
}

void XJBG::doTarJan(int u) {
  m_dfn[u] = m_low[u] = ++m_tarjanCount;
  m_inStack[u] = true;
  m_stack.push(u);

  for (auto &it : m_Sons[u]) {
    int v = std::get<0>(it);
    if (!m_dfn[v]) {
      this->doTarJan(v);
      m_low[u] = std::min(m_low[u], m_low[v]);
    } else if (m_inStack[v]) {
      m_low[u] = std::min(m_low[u], m_dfn[v]);
    }
  }

  if (m_dfn[u] == m_low[u]) {
    std::vector<int> tmp;
    while (true) {
      int cur = m_stack.top();
      m_stack.pop();
      tmp.emplace_back(cur);
      m_inStack[cur] = false;
      if (cur == u) break;
    }
    if (tmp.size() > 2) {
      ++m_useScc;
      m_Circles.insert(m_Circles.end(), tmp.begin(), tmp.end());
    }
    ++m_scc;
  }
}
void XJBG::TarJan() {
  ScopeTime t;

  for (int i = 0; i < m_IDDom.size(); ++i) {
    if (!m_dfn[i] && !m_Sons[i].empty()) {
      this->doTarJan(i);
    }
  }
  std::sort(m_Circles.begin(), m_Circles.end(),
            [&](const int &x, const int &y) {
              return this->GetID(x) < this->GetID(y);
            });

#ifdef LOCAL
  std::cerr << "@ TarJan: (scc: " << m_scc << ", usescc: " << m_useScc << ") #";
  t.LogTime();
#endif
}

void XJBG::BackSearch(ThreadData &Data, int &u, int dep) {
  for (auto &it : m_Fathers[u]) {
    int v = std::get<0>(it);
    if (m_category[v] != Data.startctg) {
      Data.vis[v] = true;
      continue;
    }
    if (Data.vis[v]) continue;
    if (dep == 0) {
      Data.OneReachable[v] = true;
      Data.TwoReachable[v] = true;
      Data.ThreeReachable[v] = true;
    } else if (dep == 1) {
      Data.TwoReachable[v] = true;
      Data.ThreeReachable[v] = true;
    } else if (dep == 2) {
      Data.ThreeReachable[v] = true;
      continue;
    }
    Data.vis[v] = true;
    this->BackSearch(Data, v, dep + 1);
    Data.vis[v] = false;
  }
}

inline void XJBG::SaveCircle(ThreadData &Data, const int &dep) {
  Data.Answer[dep - 3].emplace_back(Data.tempPath);
  ++Data.answers;
}
bool XJBG::judge(ThreadData &Data, const int &v, const int &dep) {
  if (Data.vis[v] || dep > 6) {
    return true;
  } else if (dep == 4 && !Data.ThreeReachable[v]) {
    return true;
  } else if (dep == 5 && !Data.TwoReachable[v]) {
    return true;
  } else if (dep == 6) {
    if (Data.OneReachable[v]) {
      this->SaveCircle(Data, dep + 1);
    }
    return true;
  } else {
    return false;
  }
}
void XJBG::doFindCircle(ThreadData &Data, int u, int dep) {
  for (auto &it : m_Sons[u]) {
    int v = std::get<0>(it);
    Data.tempPath[dep] = v;
    if (dep > 2 && v == Data.start) {
      this->SaveCircle(Data, dep);
      continue;
    } else if (this->judge(Data, v, dep)) {
      continue;
    }
    Data.vis[v] = true;
    this->doFindCircle(Data, v, dep + 1);
    Data.vis[v] = false;
  }
}

void XJBG::FindCircle() {
  ScopeTime t;

  for (auto &v : m_Circles) {
    std::sort(
        m_Sons[v].begin(), m_Sons[v].end(),
        [&](const std::tuple<int, int> &e1, const std::tuple<int, int> &e2) {
          return this->GetID(std::get<0>(e1)) < this->GetID(std::get<0>(e2));
        });
  }

  auto foo = [&](int pid, int start, int end) {
    auto &Data = ThreadsData[pid];
    Data.vis = std::vector<bool>(m_IDDom.size(), false);
    for (int i = 0; i < start; ++i) Data.vis[m_Circles[i]] = true;

    for (int i = start; i < end; ++i) {
      Data.OneReachable = std::vector<bool>(m_IDDom.size(), false);
      Data.TwoReachable = std::vector<bool>(m_IDDom.size(), false);
      Data.ThreeReachable = std::vector<bool>(m_IDDom.size(), false);

      int v = m_Circles[i];
      Data.start = v;
      Data.startctg = m_category[v];
      Data.vis[v] = true;
      this->BackSearch(Data, v, 0);
      Data.tempPath[0] = v;
      this->doFindCircle(Data, v, 1);
    }
  };

  std::vector<std::thread> Threads(NTHREAD);
  ThreadsData = std::vector<ThreadData>(NTHREAD, ThreadData(m_IDDom.size()));

  int sum = 0;
  for (int i = 0; i < NTHREAD; ++i) sum += PARAM[i];
  int start = 0, block = m_Circles.size() / sum;

  for (int i = 0; i < NTHREAD; ++i) {
    int end = (i == NTHREAD - 1 ? m_Circles.size() : start + block * PARAM[i]);
    Threads[i] = std::thread(foo, i, start, end);
    start += block * PARAM[i];
  }
  for (auto &it : Threads) it.join();
  for (auto &it : ThreadsData) m_answers += it.answers;

#ifdef LOCAL
  std::cerr << "@ Answer: " << m_answers << " #";
  t.LogTime();
  for (int i = 0; i < NTHREAD; ++i) {
    const auto &data = ThreadsData[i];
    std::cerr << "* " << i << ": " << data.answers << "(";
    for (int j = 0; j < 4; ++j) {
      std::cerr << data.Answer[j].size() << ", ";
    }
    std::cerr << data.Answer[4].size() << ")\n";
  }
#endif
}

void XJBG::SaveAnswer() {
  ScopeTime t;

  struct PreBuffer {
    char str[10];
    int start;
    int length;
  };
  std::vector<PreBuffer> MapID(m_IDDom.size());
  for (int i = 0; i < m_IDDom.size(); ++i) {
    int x = m_IDDom[i];
    if (x == 0) {
      MapID[i].start = 9;
      MapID[i].str[9] = '0';
      MapID[i].length = 1;
      continue;
    }
    int idx = 10;
    while (x) {
      MapID[i].str[--idx] = x % 10 + '0';
      x /= 10;
    }
    MapID[i].start = idx;
    MapID[i].length = 10 - idx;
  }

  struct T {
    char *buffer[5];
    uint32_t length[5];
    uint32_t totalLength = 0;
  };
  std::vector<T> OutData(NTHREAD);

  auto foo = [&](int pid) {
    const auto &ThData = ThreadsData[pid];
    auto &out = OutData[pid];
    for (int len = 0; len < 5; ++len) {
      const auto &ans = ThData.Answer[len];
      uint32_t tbufsize = (uint32_t)ans.size() * (len + 3) * 11;
      out.buffer[len] = new char[tbufsize];
      uint32_t tidx = 0;
      for (auto &it : ans) {
        // 便利一行len+3个数字
        for (int i = 0; i < len + 3; ++i) {
          auto &mpid = MapID[it[i]];
          if (i != 0) out.buffer[len][tidx++] = ',';
          memcpy(out.buffer[len] + tidx, mpid.str + mpid.start, mpid.length);
          tidx += mpid.length;
        }
        out.buffer[len][tidx++] = '\n';
      }
      out.length[len] = tidx;
      out.totalLength += tidx;
    }
  };

  std::vector<std::thread> Threads(NTHREAD);
  for (int i = 0; i < NTHREAD; ++i) {
    Threads[i] = std::thread(foo, i);
  }
  for (auto &it : Threads) it.join();

  int x = m_answers, firIdx = 12;
  char firBuf[12];
  firBuf[--firIdx] = '\n';
  if (x == 0) {
    firBuf[--firIdx] = '0';
  } else {
    while (x) {
      firBuf[--firIdx] = x % 10 + '0';
      x /= 10;
    }
  }

  uint32_t bufsize = 12 - firIdx;
  for (auto &it : OutData) bufsize += it.totalLength;
  int fd = open(RESULT, O_RDWR | O_CREAT, 0666);
  char *result =
      (char *)mmap(NULL, bufsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  ftruncate(fd, bufsize);
  close(fd);

  memcpy(result, firBuf + firIdx, 12 - firIdx);
  uint32_t idx = 12 - firIdx;
  for (int len = 0; len < 5; ++len) {
    for (auto &data : OutData) {
      memcpy(result + idx, data.buffer[len], data.length[len]);
      idx += data.length[len];
    }
  }

#ifdef LOCAL
  std::cerr << "@ WriteAnswer # ";
  t.LogTime();
#endif
}

int main() {
  std::cerr << std::fixed << std::setprecision(3);

  XJBG *xjbg = new XJBG();
  xjbg->LoadData();
  xjbg->TarJan();
  xjbg->FindCircle();
  xjbg->SaveAnswer();
  return 0;
}