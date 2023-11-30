[numthreads(1, 1, 1)]
void main() {
  bool cond = false;

  while (cond) {
    if (cond) {
      if (cond) {
        if (cond) {
          if (cond) {
            if (cond) {
              if (cond) {
                break;
              }
            }
            else
              break;
          }
          else
            break;
        }
        else
          break;
      }
      else
        break;
    }
    else
      break;
  }
}
