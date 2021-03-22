#include <iostream>       // std::cout
#include <atomic>         // std::atomic
#include <thread>         // std::thread
#include <vector>         // std::vector
#include <deque>          // std::deque
#include <mutex>          // std::mutex

struct Pair {
    std::vector<int>* pointer;
    long              ref_count;
}; // __attribute__((aligned(16),packed));
// for some compilers alignment needed to stop std::atomic<Pair>::load to segfault

class LFSV {
    std::atomic< Pair > pdata;
    public:

    LFSV() : pdata( Pair{ new std::vector<int>, 1 } ) {
//        std::cout << "Is lockfree " << pdata.is_lock_free() << std::endl;
    }   

    ~LFSV() { 
        Pair temp = pdata.load();
        std::vector<int>* p = temp.pointer;
        delete p;
    }

    void Insert( int const & v ) {
        Pair pdata_new, pdata_old;
        pdata_new.pointer  = nullptr;
        do {
            delete pdata_new.pointer;
            pdata_old = pdata.load();
            pdata_new.pointer   = new std::vector<int>( *pdata_old.pointer ); // pdata_old may be deleted

            // working on a local copy
            std::vector<int>::iterator b = pdata_new.pointer->begin();
            std::vector<int>::iterator e = pdata_new.pointer->end();
            if ( b==e || v>=pdata_new.pointer->back() ) { pdata_new.pointer->push_back( v ); } //first in empty or last element
            else {
                for ( ; b!=e; ++b ) {
                    if ( *b >= v ) {
                        pdata_new.pointer->insert( b, v );
                        break;
                    }
                }
            }

        } while ( !(this->pdata).compare_exchange_weak( pdata_old, pdata_new  ));
        delete pdata_old.pointer;
    }

    int operator[] ( int pos ) { // not a const method anymore
        Pair pdata_new, pdata_old;
//        std::cout << "Read from " << pdata_new.pointer;
        int ret_val = (*pdata_new.pointer) [pos];
        return ret_val;
    }
};
