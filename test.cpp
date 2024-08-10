int _calc_lvl(int size)
{
    int result = 0;
    int temp = size;
    while (temp >>= 1)
        result++;
    
    if ((1 << result) < size)
        ++result;
    return result - 7 < 0 ? 0 : result-7;
}

int main(int argc, char const *argv[])
{
    int lvl = _calc_lvl(50);
    return 0;
}
