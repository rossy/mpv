int main(void)
{
    __asm__ volatile ("movntdqa (%%rax), %%xmm0" ::: "rax", "xmm0");
}
