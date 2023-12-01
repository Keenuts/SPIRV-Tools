[numthreads(1, 1, 1)]
void main() {
  bool cond = false;

  do {
    if (cond) {
      return;
    }
  } while(cond);
}
