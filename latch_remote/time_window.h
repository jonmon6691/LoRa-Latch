#ifndef TIME_WINDOW_H
#define TIME_WINDOW_H

struct timer {
  unsigned long start_time;
  unsigned long end_time;
};


/* check_timer - handles the possbility of an overflow in a timer
Truth table: ([s]tart, [n]ow, [e]nd)
0                    Max
|.....s--n-----e.....| true
|.....s--------e.n...| false
|---e...........s--n-| true
|-n-e...........s----| true
|---e.n.........s----| false
*/
bool check_timer(struct timer t, unsigned long now) {
  if (t.start_time <= t.end_time) { // No overflow
    return now <= t.end_time && t.start_time < now;
  } else { // Overflow
    return now <= t.end_time || t.start_time <= now;
  }
}


void set_timer(struct timer* t, unsigned long now, unsigned long duration) {
  t->start_time = now;
  t->end_time = now + duration;
}


void clear_timer(struct timer* t) {
  t->end_time = t->start_time = 0;
}

#endif
