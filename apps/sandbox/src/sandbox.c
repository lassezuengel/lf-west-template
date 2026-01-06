#include <zephyr/kernel.h>

int main(void) {

  printk("What's up?\n");

  while(true) {
    k_msleep(1000);
    printk("I'm alive...\n");
  }

  return 0;
}