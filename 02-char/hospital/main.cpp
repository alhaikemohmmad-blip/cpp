#include <iostream>
#include <cstdint>
#include <cstdlib>
#include <ctime>
using namespace std;

const int MAX = 1000;
uint16_t ids[MAX];
int saved = 0;

struct Patient
{
    uint16_t id;
    char name[30];
    char Blood_type[4];
    uint8_t age;
};

uint16_t generate_id()
{
    return rand() % 9000 + 1000;
}

bool id_taken(uint16_t id)
{
    for (int i = 0; i < saved; i++)
    {
        if (ids[i] == id)
        {
            return true;
        }
    }
    return false;
}

uint16_t new_unique_id()
{
    for (int t = 0; t < 5; t++)
    {
        uint16_t id = generate_id();
        if (id_taken(id) == false)
        {
            return id;
        }
    }
    return 0;
}

bool save_id(uint16_t id)
{
    if (saved >= MAX)
    {
        return false;
    }
    ids[saved] = id;
    saved++;
    return true;
}

int main()
{
    srand(time(0));
    Patient patient;
    int reeding = 0;
    bool alm = false;
    char again = 'y';

    while (true)
    {
        // ===== 0) is there room? =====
        if (saved >= MAX)
        {
            cout << "the hospital is full, no room for a new patient!\n";
            break;
        }

        // ===== 1) age =====
        alm = false;
        for (int i = 0; i <= 5; i++)
        {
            cout << "enter your age :";
            cin >> reeding;

            if (cin.fail() || reeding <= 0 || reeding >= 120 || cin.peek() != '\n')
            {
                cin.clear();
                cin.ignore(1000, '\n');
                cout << "sorry this age avelably try agen!\n";
            }
            else
            {
                cin.ignore(1000, '\n');
                patient.age = reeding;
                alm = true;
                break;
            }
        }
        if (alm == false)
        {
            cout << "plaese try agen later!!\n\n";
            break;
        }

        // ===== 2) name =====
        alm = false;
        for (int j = 0; j < 5; j++)
        {
            cout << "enter your name : ";
            cin.getline(patient.name, sizeof(patient.name));

            if (cin.fail())
            {
                cin.clear();
                cin.ignore(1000, '\n');
                cout << "name is too long, try again!\n";
                continue;
            }

            bool digat = true;
            for (int k = 0; patient.name[k] != '\0'; k++)
            {
                if (patient.name[k] >= '0' && patient.name[k] <= '9')
                {
                    digat = false;
                }
            }

            if (digat == false || patient.name[0] == '\0')
            {
                cout << "name can not be empty or have numbers, try again!\n";
            }
            else
            {
                alm = true;
                break;
            }
        }
        if (alm == false)
        {
            cout << "plaese try agen later!!\n\n";
            break;
        }

        // ===== 3) blood type =====
        alm = false;
        for (int b = 0; b < 4; b++)
        {
            cout << "now,please enter your blood type : ";
            cin.getline(patient.Blood_type, sizeof(patient.Blood_type));

            if (cin.fail())
            {
                cin.clear();
                cin.ignore(1000, '\n');
                cout << "this tybe blood its not correact please try agin !!\n";
                continue;
            }

            if (
                ((patient.Blood_type[0] == 'A' || patient.Blood_type[0] == 'B' || patient.Blood_type[0] == 'O') && (patient.Blood_type[1] == '+' || patient.Blood_type[1] == '-') && patient.Blood_type[2] == '\0') ||
                (patient.Blood_type[0] == 'A' && patient.Blood_type[1] == 'B' && (patient.Blood_type[2] == '+' || patient.Blood_type[2] == '-') && patient.Blood_type[3] == '\0'))
            {
                alm = true;
                break;
            }
            else
            {
                cout << "please enter a coreate blood type!!\n";
            }
        }
        if (alm == false)
        {
            cout << "plaese try agen later!!\n\n";
            break;
        }

        // ===== 4) id =====
        cout << "now we create your id \n";
        patient.id = new_unique_id();
        if (patient.id == 0)
        {
            cout << "we can not create a free id now, please try again later!\n";
            break;
        }
        if (save_id(patient.id) == false)
        {
            cout << "the hospital is full, your id was not saved!\n";
            break;
        }
        cout << "done, your id is saved\n";
        cout << "your id: " << patient.id << "\n\n";

        // ===== 5) another patient? =====
        cout << "another patient? (y/n): ";
        cin >> again;
        cin.ignore(1000, '\n');
        if (again != 'y' && again != 'Y')
        {
            break;
        }
    }

    cout << "\nsaved patients: " << saved << '\n';
    for (int i = 0; i < saved; i++)
    {
        cout << i + 1 << ") id: " << ids[i] << '\n';
    }

    return 0;
}
