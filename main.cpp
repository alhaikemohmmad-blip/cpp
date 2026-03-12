#include <iostream>
using namespace std;
float areatringle(float a, float b)
{
    float Area;

    Area = 0.5 * a * b;

    return Area;
}
int main()
{

    float x;
    float y;
    cin >> x;
    cin >> y;
    cout << areatringle(x, y) << endl;

    system("pause");

    return 0;
}