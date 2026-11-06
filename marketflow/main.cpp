#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <map>
#include <random>
#include <thread>
#include <vector>
using Clock=std::chrono::steady_clock;
struct Update {uint64_t seq, sent; uint32_t symbol,level,qty; bool ask;};
// Single producer/single consumer ownership. Release/acquire publishes slot data.
template<size_t N> class Queue {
 static_assert((N&(N-1))==0);
 std::array<Update,N> data;
 alignas(64) std::atomic<uint64_t> head{0};
 alignas(64) std::atomic<uint64_t> tail{0};
public:
 bool push(Update x){auto h=head.load(std::memory_order_relaxed); if(h-tail.load(std::memory_order_acquire)==N)return false;data[h&(N-1)]=x;head.store(h+1,std::memory_order_release);return true;}
 bool pop(Update& x){auto t=tail.load(std::memory_order_relaxed);if(t==head.load(std::memory_order_acquire))return false;x=data[t&(N-1)];tail.store(t+1,std::memory_order_release);return true;}
 bool lockfree()const{return head.is_lock_free()&&tail.is_lock_free();}
};
// Fixed tick grid: 250 levels/symbol, both bid and ask depth, 100 symbols.
struct Book {
 // Each slot publishes both quantities in a single lock-free 64-bit store.
 // Any number of concurrent readers can inspect one price without retrying.
 static_assert(std::atomic<uint64_t>::is_always_lock_free);
 std::array<std::atomic<uint64_t>,250> levels{};
 std::array<bool,250> seen{};
 int touched=0, bestBid=-1, bestAsk=250;
 uint64_t sequence=0;
 Book(){for(auto &slot:levels)slot.store(0,std::memory_order_relaxed);}
 uint64_t depth(size_t level)const{return levels.at(level).load(std::memory_order_acquire);}
 enum Result{OK,STALE,CROSSED,INVALID};
 Result apply(const Update& u){
  if(u.level>=levels.size())return INVALID;
  if(u.seq<=sequence)return STALE;
  sequence=u.seq;
  if(!seen[u.level]){seen[u.level]=true;++touched;}
  auto previous=levels[u.level].load(std::memory_order_relaxed);
  auto next=u.ask ? (uint64_t(u.qty)<<32)|uint32_t(previous)
                  : (previous & 0xffffffff00000000ULL)|u.qty;
  levels[u.level].store(next,std::memory_order_release);
  if(u.ask){
   if(u.qty)bestAsk=std::min(bestAsk,int(u.level));
   else while(bestAsk<250 && !(depth(bestAsk)>>32))++bestAsk;
  }else{
   if(u.qty)bestBid=std::max(bestBid,int(u.level));
   else while(bestBid>=0 && !uint32_t(depth(bestBid)))--bestBid;
  }
  return bestBid>=bestAsk?CROSSED:OK;
 }
};
uint64_t ns(){return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();}
void tests(){
 Book b;assert(b.apply({1,0,0,120,3,false})==Book::OK);assert(b.apply({2,0,0,121,4,true})==Book::OK);
 assert(b.apply({2,0,0,122,9,false})==Book::STALE);assert(b.depth(122)==0);
 assert(b.apply({3,0,0,121,2,false})==Book::CROSSED);assert(b.apply({4,0,0,121,0,false})==Book::OK);
 std::mt19937 rng(42);std::map<int,uint32_t> bids,asks;Book ref;
 for(uint64_t i=1;i<=100000;i++){Update u{i,0,0,uint32_t(rng()%250),uint32_t(rng()%8),bool(rng()%2)};ref.apply(u);assert((u.ask?uint32_t(ref.depth(u.level)>>32):uint32_t(ref.depth(u.level)))==u.qty);auto& m=u.ask?asks:bids;if(u.qty)m[u.level]=u.qty;else m.erase(u.level);assert(ref.bestBid==(bids.empty()?-1:bids.rbegin()->first));assert(ref.bestAsk==(asks.empty()?250:asks.begin()->first));}
 Queue<8> q;Update u{};assert(!q.pop(u));for(int i=0;i<8;i++)assert(q.push({uint64_t(i),0,0,0,0,false}));assert(!q.push({}));for(int i=0;i<8;i++){assert(q.pop(u));assert(u.seq==uint64_t(i));}assert(!q.pop(u));
}
int main(int argc,char**argv){tests();uint64_t n=argc>1?std::stoull(argv[1]):1000000;if(n<1009||n>1000000000)return 2;
 bool balanced=argc>2&&std::string(argv[2])=="balanced";
 Queue<1024> q;std::array<Book,100> books;std::vector<uint64_t> latency,alert_latency;uint64_t stale=0,crossed=0,checksum=0;auto start=Clock::now();
 std::thread producer([&]{for(uint64_t i=1;i<=n;i++){Update u{i,0,uint32_t(i%100),uint32_t(((i/100)*37+(i%100)*13)%250),uint32_t(i%11),bool((i/100)%2)};if(balanced){u.level=uint32_t(((i/200)*37+(i%100)*13)%125)+(u.ask?125:0);auto cycle=i/100;if(cycle%200==0){u.ask=false;u.level=249;u.qty=1;}else if(cycle%200==1){u.ask=false;u.level=249;u.qty=0;}}if(i%997==0)u.seq=1;if(i%1009==0)u.sent=ns();while(!q.push(u))std::this_thread::yield();}});
 for(uint64_t i=0;i<n;i++){Update u;while(!q.pop(u))std::this_thread::yield();auto r=books[u.symbol].apply(u);stale+=r==Book::STALE;crossed+=r==Book::CROSSED;checksum+=books[u.symbol].sequence;if(u.sent){auto delay=ns()-u.sent;latency.push_back(delay);if(r==Book::STALE||r==Book::CROSSED)alert_latency.push_back(delay);}}
 producer.join();int touched=0;for(const auto &book:books)touched+=book.touched;double seconds=std::chrono::duration<double>(Clock::now()-start).count();std::sort(latency.begin(),latency.end());std::sort(alert_latency.begin(),alert_latency.end());
 std::cout<<"{\"workload\":\""<<(balanced?"balanced with transient crosses":"adversarial crossed book")<<"\",\"sampled_alert_p99_ms\":"<<(alert_latency.empty()?"null":std::to_string(alert_latency[size_t(.99*(alert_latency.size()-1))]/1e6))<<",\"alert_latency_samples\":"<<alert_latency.size()<<",\"updates\":"<<n<<",\"price_levels_updated\":"<<touched<<",\"atomic_l2_depth\":true,\"symbols\":100,\"price_levels_total\":25000,\"seconds\":"<<seconds<<",\"updates_per_second\":"<<n/seconds<<",\"sampled_enqueue_to_detection_p99_ms\":"<<latency[size_t(.99*(latency.size()-1))]/1e6<<",\"latency_samples\":"<<latency.size()<<",\"stale_events\":"<<stale<<",\"crossed_events\":"<<crossed<<",\"checksum\":"<<checksum<<",\"queue_atomics_lock_free\":"<<(q.lockfree()?"true":"false")<<"}\n";
}
