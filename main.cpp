#include <iostream>
#include <cmath>
using namespace std;
float circle(double d)
{
    double area;

    area = (3.14 * pow(d, 2)) / 4;

    return area;
}
int main()
{

    double a;
    cin >> a;
    cout << circle(a) << endl;

    system("pause");

    return 0;
}