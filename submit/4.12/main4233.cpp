#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
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
#define TRAIN "../data/1004812/test_data.txt"
#define RESULT "../data/1004812/result.txt"
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

class XJBG {
  struct PreBuffer {
    char str[10];
    int start;
    int length;
  };

 public:
  void LoadData();           // 加载数据
  void TarJan();             // TarJan
  void FindCircle(int pid);  // 找环
  void PreSave();            // 预处理ID
  void SaveAnswer(int pid);  // 保存答案
  void SortEdge();           // 儿子排序
  void WaitFork();           // 等待进程

 private:
  inline void addEdge(int u, int v, int w);  // 加边
  void doTarJan(int u);                      // tarjan
  bool judge(const int &u, const int &dep);  // dfs剪枝
  inline void SaveCircle(const int &dep);    // 保存环
  void BackSearch(int &st, int dep);         // 反向剪枝
  void doFindCircle(int u, int dep);         // 正向找环
  void createAnswerBuffers(int pid);         // 解析int

 public:
  static const int MAXN = 200000 + 7;        // 总点数
  static const int NTHREAD = 4;              // 线程个数
  const int PARAM[NTHREAD] = {2, 3, 5, 40};  // 线程权重
 private:
  int m_maxID = 0;                              // 最大点
  std::vector<int> m_Circles;                   // 大于3的联通分量的点
  int m_Edges[MAXN][3][100];                    // u->[son, father, weight]
  int m_CountSons[MAXN], m_CountFathers[MAXN];  // 边数目
  int m_edgeNum = 0;                            // 边数目
  int m_dfn[MAXN], m_low[MAXN];                 // Tarjan
  std::stack<int> m_stack;                      // Tarjan
  int m_category[MAXN];                         // 所在联通分量id
  bool m_inStack[MAXN];                         // Tarjan标记在不在栈内
  int m_tarjanCount = 0;                        // Tarjan搜索顺序
  int m_stackTop = 0;                           // Tarjan栈top标号
  int m_scc = 0, m_useScc = 0;                  // 总联通分量和大于3的
  std::vector<PreBuffer> m_MapID;               // 预处理答案

 private:
  int m_answers = 0;                          // 环个数
  int m_start = 0;                            // 起点
  int m_startctg = 0;                         // sccID
  std::vector<int> m_tempPath;                // dfs临时路径
  std::vector<bool> m_OneReachable;           // 一步可达
  std::vector<bool> m_TwoReachable;           // 两步可达
  std::vector<bool> m_ThreeReachable;         // 三步可达
  std::vector<std::vector<int>> m_Answer[5];  // 结果
  std::vector<bool> m_vis;                    // 标记
  char *m_buffer[5];                          // 答案
  uint32_t m_length[5];                       // 答案长度
  uint32_t m_totalLength = 0;                 // 答案总长度

 private:
  int m_TotalAnswers = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0600);
  int m_FlagFork = shmget(IPC_PRIVATE, NTHREAD * sizeof(int), IPC_CREAT | 0600);
  int *m_TotalAnswersPtr;
  int *m_FlagForkPtr;
  std::string m_debug[NTHREAD] = {"", "\t\t", "\t\t\t", "\t\t\t\t"};
};

inline void XJBG::addEdge(int u, int v, int w) {
  m_Edges[u][0][m_CountSons[u]] = v;
  m_Edges[u][2][m_CountSons[u]++] = w;
  m_Edges[v][1][m_CountFathers[v]++] = u;
  ++m_edgeNum;
  m_maxID = std::max(m_maxID, std::max(u, v) + 1);
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
  std::cerr << "@ LoadData: (V: " << m_maxID << ", E: " << m_edgeNum << ") #";
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

  for (int i = 0; i < m_CountSons[u]; ++i) {
    int v = m_Edges[u][0][i];
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

  for (int i = 0; i < m_maxID; ++i) {
    if (!m_dfn[i] && m_CountSons[i] > 0) {
      this->doTarJan(i);
    }
  }
  std::sort(m_Circles.begin(), m_Circles.end());

#ifdef LOCAL
  std::cerr << "@ TarJan: (scc: " << m_scc << ", usescc: " << m_useScc << ") #";
  t.LogTime();
#endif
}

void XJBG::BackSearch(int &u, int dep) {
  for (int i = 0; i < m_CountFathers[u]; ++i) {
    int v = m_Edges[u][1][i];
    if (m_category[v] != m_startctg) {
      m_vis[v] = true;
      continue;
    }
    if (m_vis[v]) continue;
    // m_distance[v] = std::min(m_distance[v], dep + 1);
    if (dep == 0) {
      m_OneReachable[v] = true;
      m_TwoReachable[v] = true;
      m_ThreeReachable[v] = true;
    } else if (dep == 1) {
      m_TwoReachable[v] = true;
      m_ThreeReachable[v] = true;
    } else if (dep == 2) {
      m_ThreeReachable[v] = true;
      continue;
    }
    m_vis[v] = true;
    this->BackSearch(v, dep + 1);
    m_vis[v] = false;
  }
}

// 0,1,2,3,4,5,6,0
inline void XJBG::SaveCircle(const int &dep) {
  m_Answer[dep - 3].emplace_back(m_tempPath);
  ++m_answers;
}

// 1,2,3,4,5,6,7,1
bool XJBG::judge(const int &v, const int &dep) {
  if (m_vis[v]) return true;

  switch (dep) {
    case 4:
      if (!m_ThreeReachable[v]) return true;
      return false;
      break;
    case 5:
      if (!m_TwoReachable[v]) return true;
      return false;
      break;
    case 6:
      if (m_OneReachable[v]) {
        this->SaveCircle(dep + 1);
      }
      return true;
      break;
    default:
      return false;
      break;
  }
}
void XJBG::doFindCircle(int u, int dep) {
  for (int i = 0; i < m_CountSons[u]; ++i) {
    int v = m_Edges[u][0][i];
    m_tempPath[dep] = v;
    if (dep > 2 && v == m_start) {
      this->SaveCircle(dep);
      continue;
    } else if (this->judge(v, dep)) {
      continue;
    }
    m_vis[v] = true;
    this->doFindCircle(v, dep + 1);
    m_vis[v] = false;
  }
}

void XJBG::SortEdge() {
  for (auto &v : m_Circles) {
    int count = m_CountSons[v];
    for (int j = 0; j < count; ++j) {
      int minIndex = j;
      for (int k = minIndex + 1; k < count; ++k) {
        if (m_Edges[v][0][k] < m_Edges[v][0][minIndex]) {
          minIndex = k;
        }
      }
      std::swap(m_Edges[v][0][j], m_Edges[v][0][minIndex]);
      std::swap(m_Edges[v][2][j], m_Edges[v][2][minIndex]);
    }
  }
}

void XJBG::FindCircle(int pid) {
  ScopeTime t;

#ifdef LOCAL
  std::cerr << m_debug[pid] << pid << ": FindStart\n";
#endif

  m_TotalAnswersPtr = (int *)shmat(m_TotalAnswers, NULL, 0);
  m_FlagForkPtr = (int *)shmat(m_FlagFork, NULL, 0);

  int sum = 0;
  for (int i = 0; i < NTHREAD; ++i) sum += PARAM[i];
  int block = m_Circles.size() / sum, st = 0;
  for (int i = 0; i < pid; ++i) st += PARAM[i] * block;
  int ed = (pid == NTHREAD - 1 ? m_Circles.size() : st + block * PARAM[pid]);

  m_tempPath = std::vector<int>(7, -1);
  m_vis = std::vector<bool>(m_maxID, false);
  for (int i = 0; i < st; ++i) m_vis[m_Circles[i]] = true;

  for (int i = st; i < ed; ++i) {
    m_OneReachable = std::vector<bool>(m_maxID, false);
    m_TwoReachable = std::vector<bool>(m_maxID, false);
    m_ThreeReachable = std::vector<bool>(m_maxID, false);

    int v = m_Circles[i];
    m_start = v;
    m_startctg = m_category[v];
    m_vis[v] = true;
    this->BackSearch(v, 0);
    m_tempPath[0] = v;
    this->doFindCircle(v, 1);
  }
  this->createAnswerBuffers(pid);
  *m_TotalAnswersPtr += m_answers;
  m_FlagForkPtr[pid] = 1;
#ifdef LOCAL
  std::cerr << m_debug[pid] << pid << ": FindEnd\n";
  std::cerr << m_debug[pid] << pid << ": " << m_answers << "\n";
  std::cerr << m_debug[pid] << pid << ": ";
  t.LogTime();
#endif
}

void XJBG::PreSave() {
  m_MapID.resize(m_maxID);
  for (int i = 0; i < m_maxID; ++i) {
    int x = i;
    if (x == 0) {
      m_MapID[i].start = 9;
      m_MapID[i].str[9] = '0';
      m_MapID[i].length = 1;
      continue;
    }
    int idx = 10;
    while (x) {
      m_MapID[i].str[--idx] = x % 10 + '0';
      x /= 10;
    }
    m_MapID[i].start = idx;
    m_MapID[i].length = 10 - idx;
  }
}
void XJBG::createAnswerBuffers(int pid) {
  for (int len = 0; len < 5; ++len) {
    const auto &ans = m_Answer[len];
    uint32_t tbufsize = (uint32_t)ans.size() * (len + 3) * 11;
    m_buffer[len] = new char[tbufsize];
    uint32_t tidx = 0;
    for (auto &it : ans) {
      // 便利一行len+3个数字
      for (int i = 0; i < len + 3; ++i) {
        auto &mpid = m_MapID[it[i]];
        if (i != 0) m_buffer[len][tidx++] = ',';
        memcpy(m_buffer[len] + tidx, mpid.str + mpid.start, mpid.length);
        tidx += mpid.length;
      }
      m_buffer[len][tidx++] = '\n';
    }
    m_length[len] = tidx;
    m_totalLength += tidx;
  }
}
void XJBG::SaveAnswer(int pid) {
  int answers = *m_TotalAnswersPtr;
  if (pid == 0) {
    int firIdx = 12;
    char firBuf[12];
    firBuf[--firIdx] = '\n';
    if (answers == 0) {
      firBuf[--firIdx] = '0';
    } else {
      while (answers) {
        firBuf[--firIdx] = answers % 10 + '0';
        answers /= 10;
      }
    }
    int fd = open(RESULT, O_RDWR | O_CREAT, 0666);
    char *result = (char *)mmap(NULL, 12 - firIdx, PROT_READ | PROT_WRITE,
                                MAP_SHARED, fd, 0);
    ftruncate(fd, 12 - firIdx);
    memcpy(result, firBuf + firIdx, 12 - firIdx);
    close(fd);
  }
  m_FlagForkPtr = (int *)shmat(m_FlagFork, NULL, 0);
  for (int len = 0; len < 5; ++len) {
    if (pid == 0 && len == 0) {
      m_buffer[len][m_length[len]] = 0;
      FILE *fp = fopen(RESULT, "at");
      fputs(m_buffer[len], fp);
      fclose(fp);
      m_FlagForkPtr[pid] = len;
    }
    if (pid == 0 && len != 0) {
      while (m_FlagForkPtr[NTHREAD - 1] != len - 1) {
        usleep(1);
      }
      m_buffer[len][m_length[len]] = 0;
      FILE *fp = fopen(RESULT, "at");
      fputs(m_buffer[len], fp);
      fclose(fp);
      m_FlagForkPtr[pid] = len;
    }
    if (pid != 0) {
      while (m_FlagForkPtr[pid - 1] != len) {
        usleep(1);
      }
      m_buffer[len][m_length[len]] = 0;
      FILE *fp = fopen(RESULT, "at");
      fputs(m_buffer[len], fp);
      fclose(fp);
      m_FlagForkPtr[pid] = len;
    }
  }
}

void XJBG::WaitFork() {
  while (true) {
    int x = 0;
    for (int i = 0; i < NTHREAD; ++i) x += m_FlagForkPtr[i];
    if (x == NTHREAD) {
      return;
    }
    usleep(1);
  }
}

int main() {
  std::cerr << std::fixed << std::setprecision(3);

  XJBG *xjbg = new XJBG();
  xjbg->LoadData();
  xjbg->PreSave();
  xjbg->TarJan();
  xjbg->SortEdge();

  pid_t Children[XJBG::NTHREAD - 1] = {0};
  int pid = 0;
  for (int i = 0; i < XJBG::NTHREAD - 1; i++) {
    if (pid == 0) {
      int x = fork();
      Children[i] = x;
      if (x == 0) {
        pid = i + 1;
        break;
      }
    }
  }

  xjbg->FindCircle(pid);
  xjbg->WaitFork();
  xjbg->SaveAnswer(pid);

  if (pid != 0) exit(0);
  return 0;
}