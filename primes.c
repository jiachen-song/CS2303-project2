#include <stdio.h>
#include <stdbool.h>

// 函数：判断一个数是否为素数
bool isPrime(int n) {
    if (n <= 1) {
        return false;
    }
    if (n <= 3) {
        return true;
    }
    if (n % 2 == 0 || n % 3 == 0) {
        return false;
    }
    
    // 只需检查到 sqrt(n)
    for (int i = 5; i * i <= n; i += 6) {
        if (n % i == 0 || n % (i + 2) == 0) {
            return false;
        }
    }
    return true;
}

int main() {
    int count = 0;  // 记录找到的素数个数
    int num = 2;    // 从 2 开始检查
    
    printf("前 20 个素数是：\n");
    
    while (count < 20) {
        if (isPrime(num)) {
            printf("%d ", num);
            count++;
            
            // 每行打印 10 个数字
            if (count % 10 == 0) {
                printf("\n");
            }
        }
        num++;
    }
    
    printf("\n");
    printf("输出完毕！\n");
    return 0;
}
