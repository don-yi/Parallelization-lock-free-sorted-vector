#include <iostream>       // std::cout
#include <atomic>         // std::atomic
#include <thread>         // std::thread
#include <vector>         // std::vector
#include <deque>          // std::deque
#include <mutex>          // std::mutex

/**
 * \brief Custom queue style memory manager to avoid ABA problem
 * It uses deque
 * to store "std::vector<int>*", adds in the back, removes from front
 */
class MemoryBank {
  std::deque< std::vector<int>* > slots;
  std::mutex m;
public:

  /**
   * \brief c'tor, 6k is magic num (num ptr)
   */
  MemoryBank() : slots(6000) {
    for (int i = 0; i < 6000; ++i)
      slots[i]
      = reinterpret_cast<std::vector<int>*>(new char[sizeof(std::vector<int>)]);
  }

  /**
   * \brief get 1st (front) ptr in slot for new alloc
   * \return ptr to vec
   */
  std::vector<int>* Get() {
    std::lock_guard<std::mutex> lock(m);
    std::vector<int>* p = slots[0];
    slots.pop_front();
    return p;
  }

  /**
   * \brief store used and dealloc'ed ptr to back of deque
   * \param p dealloc'ed ptr to push back to slot
   */
  void Store(std::vector<int>* p) {
    std::lock_guard<std::mutex> lock(m);
    slots.push_back(p);
  }

  /**
   * \brief d'tor
   */
  ~MemoryBank() {
    for (auto& el : slots) delete[] reinterpret_cast<char*>(el);
  }
};

/**
 * \brief data w/ ref ct
 */
struct Pair {
  std::vector<int>* pointer;
  long              ref_count;
}; // __attribute__((aligned(16),packed));
// for some compilers alignment needed to stop std::atomic<Pair>::load to segfault

/**
 * \brief impl of lfsv w/:
 * custom queue-style memory manager to solve aba prob and
 * ref ct to prevent del'ing data while being read
 */
class LFSV {

  MemoryBank mb;
  std::atomic< Pair > data;

public:

  /**
   * \brief c'tor w/ ref ct 1
   */
  LFSV() : data(Pair{ new ( mb.Get() ) std::vector<int>, 1 }) {
    //        std::cout << "Is lockfree " << pdata.is_lock_free() << std::endl;
  }
 
  /**
   * \brief d'tor to dealloc data
   */
  ~LFSV() {
    Pair const temp = data.load();
    std::vector<int>* p = temp.pointer;
    p->~vector();
  }

  /**
   * \brief replaces the data with a new one (ins'ed)
   * but only in the window of opportunity when the reference count is 1
   * \param v ins pos
   */
  void Insert(int const& v) {

    Pair data_new{}, data_old{};
    // old.second (the count) is never assigned from data_.second;
    // it is always 1
    data_old.ref_count = 1;
    data_new.pointer = nullptr;
    data_new.ref_count = 1;
    // optim var
    std::vector<int>* pdata_last = nullptr;

    do {

      data_old = data.load();

      // avoid rebuilding the data over and over again
      // if the old map hasn¡¯t been replaced (only the count).
      if (pdata_last == data_old.pointer) continue;

      // dealloc prev new data
      if (data_new.pointer) {
        data_new.pointer->~vector();
        mb.Store(data_new.pointer);
      }

      // deep cp old data (linear time)
      data_new.pointer = new (mb.Get()) std::vector<int>(*data_old.pointer);

      // working on a local copy
      std::vector<int>::iterator b = data_new.pointer->begin();
      std::vector<int>::iterator e = data_new.pointer->end();
      //first in empty or last element
      if (b == e || v >= data_new.pointer->back()) 
        data_new.pointer->push_back(v);
      else {
        for (; b != e; ++b)
          if (*b >= v) { data_new.pointer->insert(b, v); break; }
      }

      // diff data now, so up last data
      pdata_last = data_old.pointer;

    // Update will loop
    // until it has a window of opportunity of replacing a pointer
    // with a counter of 1, with another pointer having a counter of 1.
    } while (
      !data.compare_exchange_weak( data_old, data_new));

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

  /**
   * \brief lookup (reader); incr ref ct b4 access, decr it after
   * to prevent dealloc'ing while reading
   * \param pos elem pos to read
   * \return data stored in pos
   */
  int operator[] (int pos) {

    Pair data_new{}, data_old{};

    // increment the reference count before accessing the data
    do {
      data_old = data.load();
      data_new = data_old;
      ++data_new.ref_count;
    } while (
      !data.compare_exchange_weak(data_old, data_new));

    // read data
    int const ret_val = (*data_new.pointer)[pos];

    // decr ref ct after read
    do {
      data_old = data.load();
      data_new = data_old;
      --data_new.ref_count;
    } while (
      !data.compare_exchange_weak(data_old, data_new));

    return ret_val;
  }
};

