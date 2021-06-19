#include <magic.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
namespace fs = std::filesystem;

static std::atomic_size_t numThreadsAvailable =
    std::thread::hardware_concurrency();

class Scanner final {
 private:
#if 0
  enum FileType : std::string {
    Other = "__other__",
    SymLink = "__symlink__",
    Error = "__error__"
  };
#endif

  std::vector<std::unique_ptr<Scanner>> _scanners;
  std::unordered_map<std::string, size_t> _stats;
  using StatsElt = std::pair<std::string, size_t>;
  fs::path _path;
  std::thread *_thread;
  magic_t _magic;
  size_t _numScanned;

  const std::unordered_map<std::string, size_t> &getStats() const {
    return _stats;
  };

  void clearStats() { _stats.clear(); }

  void addMagic(const fs::path &p) {
    const char *desc;
    if (_magic && (desc = magic_file(_magic, p.c_str()))) {
      std::string s(desc);
      ++_stats[s.substr(0, s.find_first_of(","))];
    } else
      ++_stats["__magic_error__"];
  }

  void joinThreads() {
    if (_thread) {
#ifdef DEBUG
      std::cout << "[+] Joining " << _thread->get_id() << std::endl;
#endif
      _thread->join();
      delete _thread;
      _thread = nullptr;
      ++numThreadsAvailable;
    }

    for (auto &s : _scanners) {
      s->joinThreads();
      for (const auto &pr : s->getStats()) _stats[pr.first] += pr.second;
      s->clearStats();
    }
    magic_close(_magic);
    _magic = nullptr;
  }

  void startThread(void) {
    while (numThreadsAvailable == 0) std::this_thread::yield();
    --numThreadsAvailable;
    _thread = new std::thread([&]() { scanImpl(); });
#ifdef DEBUG
    std::cout << "[+] Starting " << _thread->get_id() << std::endl;
#endif
  }

  void scanImpl() {
    try {
      for (auto &item : fs::directory_iterator(_path)) {
        ++_numScanned;
        auto itemPath = item.path();
        try {
          if (fs::is_symlink(itemPath))
            ++_stats["__symlink__"];  // FileType::SymLink];
          else if (fs::is_directory(itemPath)) {
            auto s = std::make_unique<Scanner>(itemPath);
            s->scan();
            _scanners.push_back(std::move(s));
          } else if (fs::is_regular_file(itemPath))
            addMagic(itemPath);
          else
            ++_stats["__other__"];                 // FileType::Other];
        } catch (const fs::filesystem_error &e) {  // Error from is_* routines.
          ++_stats["__error__"];                   // FileType::Error];
        }
      }
    } catch (const fs::filesystem_error &e) {  // Error from directory_iterator.
      ++_stats["__error__"];                   // FileType::Error];
    }
  }

 public:
  Scanner(fs::path p) : _path(p), _thread(nullptr), _numScanned(0) {
    if ((_magic = magic_open(MAGIC_NONE))) magic_load(_magic, 0);
  }

  void scan() {
    if (!numThreadsAvailable)
      scanImpl();
    else
      startThread();

    joinThreads();
  }

  void dumpResults() {
    joinThreads();

    std::vector<StatsElt> results;
    for (const auto &pr : _stats)
      results.emplace_back(std::make_pair(pr.first, pr.second));

    std::sort(results.begin(), results.end(),
              [](StatsElt &a, StatsElt &b) { return a.second > b.second; });

    std::cout << "Top " << std::min((size_t)10, results.size())
              << " file types:" << std::endl;
    for (size_t i = 0; i < 10 && i < results.size(); ++i)
      std::cout << (i + 1) << ")\t" << results[i].first << ": "
                << results[i].second << std::endl;
  }
};

static void usage(const char *execname) {
  std::cout << "Usage: " << execname << " [-n num_threads] <path to scan>"
            << std::endl;
  exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
  int opt;
  while ((opt = getopt(argc, argv, "hn:")) != -1) {
    switch (opt) {
      case 'n':
        numThreadsAvailable = std::atoi(optarg);
        break;
      case 'h':
      default:
        usage(argv[0]);
        break;
    }
  }
  if (optind >= argc) {
    std::cerr << "Usage error, a file path must be specified." << std::endl;
    return EXIT_FAILURE;
  }

  std::string root(argv[optind]);
  std::cout << "Scanning " << root
            << " (threads available: " << numThreadsAvailable << ')'
            << std::endl;

  Scanner S(root);
  S.scan();
  S.dumpResults();

  return 0;
}
