[numthreads(1, 1, 1)]
void main() {
  bool cond = false;

  // 12
  do {

    // 15
    if (cond) {
      // 18
      break;
    }

    // 17
    if (cond) {
      // 21
    } else {

      // 22
      if (cond) {
        // 25
        break;
      }
      //24

    }
    // 20

    //14
  } while (cond);

  // 13
  return;
}

