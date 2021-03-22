#include <iostream>       // std::cout
#include <atomic>         // std::atomic
#include <thread>         // std::thread
#include <vector>         // std::vector
#include <deque>          // std::deque
#include <mutex>          // std::mutex

std::atomic<int> counter(0);

class MemoryBank {
    std::deque< std::vector<int>* > slots;
    std::mutex m;
    public:
        MemoryBank() : slots(6000) {
            for ( int i=0; i<6000; ++i ) {
                slots[i] = reinterpret_cast<std::vector<int>*>( new char[ sizeof(std::vector<int>) ] );
            }
        }
        std::vector<int>* Get() {
            std::lock_guard<std::mutex> lock( m );
            std::vector<int>* p = slots[0];
            slots.pop_front();
            return p;
        }
        void Store( std::vector<int>* p ) {
            std::lock_guard<std::mutex> lock( m );
            slots.push_back( p );
        }
        ~MemoryBank() {
            for ( auto & el : slots ) { delete [] reinterpret_cast<char*>( el ); }
        }
};

class LFSV {
    MemoryBank mb;
    std::atomic< std::vector<int>* > pdata;
    std::mutex wr_mutex;
    public:

    LFSV() : mb(), pdata( new ( mb.Get() ) std::vector<int> ), wr_mutex() {
        //std::cout << "Is lockfree " << pdata.is_lock_free() << std::endl;
    }   

    ~LFSV() { 
        //delete pdata.load(); 
        pdata.load()->~vector();
        mb.Store( pdata.load() );
    }

    void Insert( int const & v ) {
        std::vector<int> *pdata_new = nullptr, *pdata_old;
        do {
            ++counter;

            //delete pdata_new;
            if ( pdata_new ) { 
                //pdata_new->~vector();
                mb.Store( pdata_new ); 
            }
            
            pdata_old = pdata;
            pdata_new = new (mb.Get()) std::vector<int>( *pdata_old );

            std::vector<int>::iterator b = pdata_new->begin();
            std::vector<int>::iterator e = pdata_new->end();
            if ( b==e || v>=pdata_new->back() ) { pdata_new->push_back( v ); } //first in empty or last element
            else {
                for ( ; b!=e; ++b ) {
                    if ( *b >= v ) {
                        pdata_new->insert( b, v );
                        break;
                    }
                }
            }
//            std::lock_guard< std::mutex > write_lock( wr_mutex );
//            std::cout << "insert " << v << "(attempt " << counter << ")" << std::endl;
        } while ( !(this->pdata).compare_exchange_weak( pdata_old, pdata_new  ));
        // if we use a simple "delete pdata_old" here, crash is almost guaranteed
        // the cause of the problem is ABA
        // using MemoryBank KIND OF solves it (for demo purposes only!)
        // it uses deque to store "std::vector<int>*", adds in the back, removes from front
        // this way there is some time before we get the same address back, so we home 
        // we will never see the same address again (ABA) in one call to Insert
        
        //pdata_old->~vector(); 
        mb.Store( pdata_old ); 

//        std::lock_guard< std::mutex > write_lock( wr_mutex );
//        std::vector<int> * pdata_current = pdata;
//        std::vector<int>::iterator b = pdata_current->begin();
//        std::vector<int>::iterator e = pdata_current->end();
//        for ( ; b!=e; ++b ) {
//            std::cout << *b << ' ';
//        }
//        std::cout << "Size " << pdata_current->size() << " after inserting " << v << std::endl;
    }

    int const& operator[] ( int pos ) const {
        return (*pdata)[ pos ];
    }
};

LFSV lfsv;

#include <algorithm>//copy, random_shuffle
#include <ctime>    //std::time (NULL) to seed srand
void insert_range( int b, int e ) {
    int * range = new int [e-b];
    for ( int i=b; i<e; ++i ) {
        range[i-b] = i;
    }
    std::srand( static_cast<unsigned int>(std::time (NULL)) );
    std::random_shuffle( range, range+e-b );
    for ( int i=0; i<e-b; ++i ) {
        lfsv.Insert( range[i] );
    }
    delete [] range;
}

int read_position_0( int how_many_times ) {
    int j = 0;
    for ( int i=0; i<how_many_times; ++i ) {
        if ( lfsv[0] != -1 ) {
            std::cout << "not -1 on iteration " << i << "\n"; // see main - all element are non-negative, so index 0 should always be -1
        }
    }
    return j;
}

// ABA is "solved" by delaying memory reuse
// but writer may still delete while reader is reading - uncomment line threads.push_back( std::thread( read_position_0, 1000000000 ) ); below
int main ()
{
    // insert one element
    std::vector<std::thread> threads;
    lfsv.Insert( -1 );
    threads.push_back( std::thread( read_position_0, 1000000000 ) );
   
    int num_threads = 4;
    int num_per_thread = 40;
    for (int i=0; i<num_threads; ++i) {
        threads.push_back( std::thread( insert_range, i*num_per_thread, (i+1)*num_per_thread ) );
    }
    for (auto& th : threads) th.join();

    for (int i=0; i<num_threads*num_per_thread; ++i) { 
        //std::cout << lfsv[i] << ' '; 
        if ( lfsv[i] != i-1 ) {
            std::cout << "Error\n";
            return 1;
        }
    }
    std::cout << "All good\n";
    std::cout << "Counter = " << counter << std::endl;
    std::cout << "Repeats = " << counter-num_threads*num_per_thread << std::endl;

    return 0;
}
