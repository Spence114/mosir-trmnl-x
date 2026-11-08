#include <initializer_list>
#include <mosir_hot_policy.h>
#include <assert.h>
#include <stdio.h>
using namespace Mosir;
int main() {
  for (int usb=0;usb<2;++usb) for (int wifi=0;wifi<2;++wifi)
  for (int opt=0;opt<2;++opt) for (int ok=0;ok<2;++ok) for (int special=0;special<2;++special)
  for (uint64_t rate : {0ULL,1ULL,5ULL,6ULL,300ULL,0x100000001ULL})
    assert(eligible(usb,wifi,opt,rate,ok,special)==(usb&&wifi&&opt&&ok&&!special&&rate>=1&&rate<=5));
  Gestures g; Gesture e;
  for (uint8_t c=0;c<3;++c) {
    g.reset();g.poll(1<<c,100);g.poll(0,140);
    assert(g.take(e)&&e.channel==c&&!e.held);assert(!g.take(e));
    g.reset();g.poll(1<<c,100);g.poll(1<<c,2099);assert(!g.take(e));
    g.poll(1<<c,2100);assert(g.take(e)&&e.channel==c&&e.held);
    g.poll(1<<c,7000);g.poll(0,7010);assert(!g.take(e));
  }
  // A hold already dispatched before a menu/handler return cannot become a second gesture.
  g.reset(2); g.poll(2,3000); g.poll(0,3100); assert(!g.take(e));
  g.poll(2,3200); g.poll(0,3250); assert(g.take(e)&&e.channel==1&&!e.held);
  // Events survive delayed consumption during network I/O; order and hold classification are retained.
  g.reset();g.poll(1,0);g.poll(0,50);g.poll(2,100);g.poll(2,2100);g.poll(0,2150);
  assert(g.take(e)&&e.channel==0&&!e.held);assert(g.take(e)&&e.channel==1&&e.held);assert(!g.take(e));
  // Wrap-around timing and bounded queue overflow.
  g.reset();g.poll(2,UINT32_MAX-999);g.poll(2,1000);assert(g.take(e)&&e.held);
  g.reset();for(int i=0;i<12;++i){g.poll(1,i*10);g.poll(0,i*10+1);}
  for(int i=0;i<8;++i) { assert(g.take(e)); }
  assert(!g.take(e));
  RefreshBudget b;assert(b.full(0,40,20000));b.completed(true,0);
  for(int i=0;i<40;++i){assert(!b.full(i+1,40,20000));b.completed(false,i+1);}
  assert(b.full(41,40,20000));b.completed(true,50);
  assert(!b.full(20049,40,20000));assert(b.full(20050,40,20000));
  b.invalidate();assert(b.full(51,40,20000));
  b.completed(true,UINT32_MAX-999);assert(b.full(19000,40,20000));
  puts("MOSIR policy, queued gestures, hold timing, and refresh limits passed.");
}
