[numthreads(1, 1, 1)]
void main() {
  bool cond = false;

  while (cond) {
    if (cond)
      return;
    if (cond)
      break;
    if (cond)
      continue;
  }
}
