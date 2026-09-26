#include <iostream>
using namespace std;
int main()
{

    const int max_size = 4;
    int password[max_size] = {1234};
    for (int i = 0; i < 3;)
    {
        cin >> password[i];

        if (cin.fail())
        {
            i++;
            cout << "pleas enter a number of password,not letter\n";

            cin.clear();
            cin.ignore(1000, '\n');
            if (i == 3)
            {
                cout << "please try agin later \n";
            }
        }
    
    else
    {
        if (password[i] < 1000 || password[i] > 9999)
        {
            cout << "please enter a 4 digiet\n";
            i++;
            if (i == 3)
            {
                cout << "try later " << endl;
            }
        }
            else
            {

                if (password[i] == 1234)
                {
                    cout << "wlcome to mox banke\n ";
                    break;
                }
                else
                {

                    cout << "the passwerd not correct  \n";
                    i++;
                    if (i == 3)
                    {
                        cout << "please try agin later \n";
                    }
                }
            }

        }
    }
    
    system("pause");

    return 0;
}
