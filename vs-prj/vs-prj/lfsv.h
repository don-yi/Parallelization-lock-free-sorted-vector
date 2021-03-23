#include <iostream>       // std::cout
#include <atomic>         // std::atomic
#include <thread>         // std::thread
#include <vector>         // std::vector
#include <deque>          // std::deque
#include <mutex>          // std::mutex

/**
 * \brief Custom queue style memory manager to avoid ABA problem
 * It uses deque to
 * store "std::vector<int>*", adds in the back, removes from front
 */
class MemoryBank {
  std::deque< std::vector<int>* > slots;
  std::mutex m;
public:

  MemoryBank() : slots(6000) {
    for (int i = 0; i < 6000; ++i)
      slots[i]
      = reinterpret_cast<std::vector<int>*>(new char[sizeof(std::vector<int>)]);
  }

  std::vector<int>* Get() {
    std::lock_guard<std::mutex> lock(m);
    std::vector<int>* p = slots[0];
    slots.pop_front();
    return p;
  }

  void Store(std::vector<int>* p) {
    std::lock_guard<std::mutex> lock(m);
    slots.push_back(p);
  }

  ~MemoryBank() {
    for (auto& el : slots) delete[] reinterpret_cast<char*>(el);
  }
};

struct Pair {
  std::vector<int>* pointer;
  long              ref_count;
}; // __attribute__((aligned(16),packed));
// for some compilers alignment needed to stop std::atomic<Pair>::load to segfault

class LFSV {
  MemoryBank mb;
  std::atomic< Pair > data;
public:

  LFSV() : data(Pair{ new ( mb.Get() ) std::vector<int>, 1 }) {
    //        std::cout << "Is lockfree " << pdata.is_lock_free() << std::endl;
  }
 
  ~LFSV() {
    Pair temp = data.load();
    std::vector<int>* p = temp.pointer;
    p->~vector();
  }

  void Insert(int const& v) {
    Pair data_new, data_old;
    data_new.pointer = nullptr;

    do {

      //delete pdata_new;
      if (data_new.pointer) {
        data_new.pointer->~vector();
        mb.Store(data_new.pointer);
      }

      data_old = data.load();
      data_new.pointer = new (mb.Get()) std::vector<int>(*data_old.pointer);

      // working on a local copy
      std::vector<int>::iterator b = data_new.pointer->begin();
      std::vector<int>::iterator e = data_new.pointer->end();
      //first in empty or last element
      if (b == e || v >= data_new.pointer->back()) data_new.pointer->push_back(v);
      else {
        for (; b != e; ++b)
          if (*b >= v) { data_new.pointer->insert(b, v); break; }
      }
    } while (!data.compare_exchange_weak(
      data_old, data_new));

    // if we use a simple "delete pdata_old" here, crash is almost guaranteed
    // the cause of the problem is ABA
    // using MemoryBank KIND OF solves it
    // it uses deque to
    // store "std::vector<int>*", adds in the back, removes from front
    // this way there is some time before we get the same address back,
    // so we home 
    // we will never see the same address again (ABA) in one call to Insert

    data_old.pointer->~vector(); 
    mb.Store(data_old.pointer);

    //        std::lock_guard< std::mutex > write_lock( wr_mutex );
    //        std::vector<int> * pdata_current = pdata;
    //        std::vector<int>::iterator b = pdata_current->begin();
    //        std::vector<int>::iterator e = pdata_current->end();
    //        for ( ; b!=e; ++b ) {
    //            std::cout << *b << ' ';
    //        }
    //        std::cout << "Size " << pdata_current->size() << " after inserting " << v << std::endl;
  }

  int operator[] (int pos) {
    Pair const data_new = data.load();
    //        std::cout << "Read from " << pdata_new.pointer;
    int const ret_val = (*data_new.pointer)[pos];
    return ret_val;
  }
};

