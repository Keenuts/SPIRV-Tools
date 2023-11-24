[numthreads(1, 1, 1)]
void main() {
  bool cond = false;

  while (cond) {
    if (cond)
      continue;

    while (cond) {
      if (cond)
        break;
    }

    if (cond)
      continue;

  }
}
