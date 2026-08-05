#include <iostream>
#include <cstring>
#include <wiringPi.h>

const int NUMBER_LINES = 8;
const int NUMBER_COLUMNS = 8;

const int LINE_PINS[NUMBER_LINES]     = {4,  5,  6, 12, 13, 16, 19, 20};
const int COLUMN_PINS[NUMBER_COLUMNS] = {21, 22, 23, 24, 25, 26, 27, 17};

bool matrix_prev[NUMBER_LINES][NUMBER_COLUMNS];
bool matrix[NUMBER_LINES][NUMBER_COLUMNS];

int main()
{
    if (wiringPiSetupGpio() < 0)
    {
        std::cerr << "Erro ao inicializar o WiringPi!" << std::endl;
        return 1;
    }

    for (int col = 0; col < NUMBER_COLUMNS; col++)
    {
        pinMode(COLUMN_PINS[col], INPUT);
        pullUpDnControl(COLUMN_PINS[col], PUD_UP);
    }

    for (int line = 0; line < NUMBER_LINES; line++)
    {
        pinMode(LINE_PINS[line], INPUT);
    }

    std::memset(matrix_prev, 0, sizeof(matrix_prev));
    std::memset(matrix, 0, sizeof(matrix));

    std::cout << "Varrendo matriz com WiringPi... Pressione CTRL+C para sair." << std::endl;

    while (true)
    {
        for (int line = 0; line < NUMBER_LINES; line++)
        {
            int line_pin = LINE_PINS[line];

            pinMode(line_pin, OUTPUT);
            digitalWrite(line_pin, LOW);

            delayMicroseconds(5); 

            for (int col = 0; col < NUMBER_COLUMNS; col++)
            {
                matrix[line][col] = !digitalRead(COLUMN_PINS[col]);
            }

            pinMode(line_pin, INPUT);
        }

        bool alteracao_detectada = false;

        for (int line = 0; line < NUMBER_LINES; line++)
        {
            for (int col = 0; col < NUMBER_COLUMNS; col++)
            {
                if (matrix[line][col] != matrix_prev[line][col])
                {
                    alteracao_detectada = true;
                    bool estado = matrix[line][col];

                    std::cout << "Mudança na posição [" << line << "][" << col << "]: "
                              << (estado ? "ATIVADO (1)" : "DESATIVADO (0)")
                              << std::endl;
                }
            }
        }

        if (alteracao_detectada)
        {
            std::memcpy(matrix_prev, matrix, sizeof(matrix));
        }

        delay(10);
    }

    return 0;
}