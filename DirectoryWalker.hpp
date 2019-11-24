#include <boost/filesystem.hpp>
#include <mutex>
#include <queue>

class DirectoryWalker {
public:
  DirectoryWalker(std::string directoryName);

  void Traverse(bool descend);
  boost::filesystem::path* GetNext();

private:
  boost::filesystem::path Directory;
  std::mutex Lock;
  std::queue<boost::filesystem::path> Queue;
};