#include <iostream>
#include <vector>
#include <algorithm>
#include <regex>
#include <map>
#include <chrono>
#include <opencv2/opencv.hpp>
#include <tesseract/baseapi.h>

using namespace std;
using namespace cv;
using namespace cv::dnn;

// Метрика редагування для компенсації можливих помилок розпізнавання літер OCR
int levenshtein(const string& s1, const string& s2) {
    int n = s1.length(), m = s2.length();
    vector<vector<int>> dp(n + 1, vector<int>(m + 1));
    for (int i = 0; i <= n; i++) dp[i][0] = i;
    for (int j = 0; j <= m; j++) dp[0][j] = j;
    for (int i = 1; i <= n; i++) {
        for (int j = 1; j <= m; j++) {
            dp[i][j] = min({dp[i - 1][j] + 1, dp[i][j - 1] + 1,
                            dp[i - 1][j - 1] + (s1[i - 1] == s2[j - 1] ? 0 : 1)});
        }
    }
    return dp[n][m];
}

// Структура для зберігання сесії транспортного засобу
struct CarSession {
    string plate;
    chrono::system_clock::time_point entryTime;
};

// Менеджер станів паркувального простору та білінгової системи
class ParkingSystem {
private:
    map<string, CarSession> activeCars;
    map<string, int> detectionBuffer;                         // Буфер послідовних кадрів для підтвердження детекції
    map<string, chrono::system_clock::time_point> cooldowns;  // Захист від повторної реєстрації одного авто

    int freeSpaces = 100;
    const double hourlyRate = 50.0;
    const int thresholdFrames = 15;                           // Поріг фіксації стабільного розпізнавання номера
    int ticketCounter = 1;

public:
    bool gateOpen = false;
    chrono::system_clock::time_point gateOpenTime;
    const int gateWaitSeconds = 5;                            // Тайм-аут відкритого положення шлагбаума
    const int cooldownSeconds = 10;                           // Тривалість ігнорування вже зліченого номера

    // Генерація резервного талона на в'їзді
    void registerTicketEntry() {
        string ticketID = "TICKET_" + to_string(ticketCounter++);
        activeCars[ticketID] = {ticketID, chrono::system_clock::now()};
        freeSpaces--;
        cout << "[В'ЇЗД ТАЛОН] " << ticketID << ". Місць залишилось: " << freeSpaces << endl;
        openGate();
    }

    // Пошук ідентифікатора у базі активних сесій за критерієм близькості Левенштейна (похибка <= 1 символ)
    string findClosestPlate(string plate) {
        for (auto const& [key, val] : activeCars) {
            if (levenshtein(plate, key) <= 1) return key;
        }
        return "";
    }

    // Перевірка стабільності розпізнавання з урахуванням виставлених коулдаунів
    bool isStableDetection(string plate) {
        if (cooldowns.count(plate)) {
            auto elapsed = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now() - cooldowns[plate]).count();
            if (elapsed < cooldownSeconds) return false;
        }

        detectionBuffer[plate]++;
        if (detectionBuffer[plate] >= thresholdFrames) {
            detectionBuffer[plate] = 0;
            return true;
        }
        return false;
    }

    void openGate() {
        gateOpen = true;
        gateOpenTime = chrono::system_clock::now();
        cout << "[ШЛАГБАУМ] ВІДКРИТО. Очікування проїзду..." << endl;
    }

    // Диспетчер автоматичного закриття виконавчого механізму за таймером
    void updateGate() {
        if (gateOpen) {
            auto now = chrono::system_clock::now();
            auto elapsed = chrono::duration_cast<chrono::seconds>(now - gateOpenTime).count();
            if (elapsed >= gateWaitSeconds) {
                gateOpen = false;
                cout << "[ШЛАГБАУМ] ЗАКРИТО." << endl;
            }
        }
    }

    // Реєстрація успішного автоматичного в'їзду за розпізнаним держномером
    void registerEntry(string plate) {
        if (freeSpaces > 0 && activeCars.find(plate) == activeCars.end()) {
            activeCars[plate] = {plate, chrono::system_clock::now()};
            cooldowns[plate] = chrono::system_clock::now();
            freeSpaces--;
            cout << "[В'ЇЗД] " << plate << ". Місць залишилось: " << freeSpaces << endl;
            openGate();
        } else if (freeSpaces <= 0) {
            cout << "[ВІДМОВА] Паркінг повний!" << endl;
        }
    }

    // Розрахунок вартості послуг та закриття сесії при штатному виїзді по OCR
    void processExit(string plate) {
        string target = findClosestPlate(plate);
        if (!target.empty()) {
            auto entry = activeCars[target].entryTime;
            auto durationMinutes = chrono::duration_cast<chrono::minutes>(chrono::system_clock::now() - entry).count();

            // Розрахунок білінгу: перші 60 хвилин безкоштовно, далі — погодинна тарифікація
            double cost = 0;
            if (durationMinutes > 60) {
                cost = ((durationMinutes - 60) / 60.0 + 1) * hourlyRate;
            }

            cout << "[ВИЇЗД] " << target << " (ocr: " << plate << "). Час: " << durationMinutes << " хв. Сплата: " << cost << " грн." << endl;
            activeCars.erase(target);
            freeSpaces++;
            cooldowns[target] = chrono::system_clock::now();
            openGate();
        } else {
            cout << "[ПОМИЛКА] Авто " << plate << " не знайдено в базі (в'їзд не був зафіксований)." << endl;
        }
    }

    // Обробка виїзду транспортного засобу за найдавнішим активним талоном у базі
    void processTicketExit() {
        string targetTicket = "";
        chrono::system_clock::time_point oldestTime = chrono::system_clock::time_point::max();

        for (auto const& [key, val] : activeCars) {
            if (key.rfind("TICKET_", 0) == 0) {
                if (val.entryTime < oldestTime) {
                    oldestTime = val.entryTime;
                    targetTicket = key;
                }
            }
        }

        if (!targetTicket.empty()) {
            auto entry = activeCars[targetTicket].entryTime;
            auto durationMinutes = chrono::duration_cast<chrono::minutes>(chrono::system_clock::now() - entry).count();

            double cost = 0;
            if (durationMinutes > 60) {
                cost = ((durationMinutes - 60) / 60.0 + 1) * hourlyRate;
            }

            cout << "[ВИЇЗД ТАЛОН] " << targetTicket << ". Час: " << durationMinutes << " хв. Сплата: " << cost << " грн." << endl;
            activeCars.erase(targetTicket);
            freeSpaces++;
            openGate();
        } else {
            cout << "[ПОМИЛКА] На парковці немає зареєстрованих автомобілів за талонами!" << endl;
        }
    }

    int getFreeSpaces() { return freeSpaces; }
};

// Евристичне виправлення синтаксичних помилок OCR на основі позиційної структури номерних знаків
string cleanPlate(string text) {
    if (text.length() < 5) return "";

    auto fixChar = [](char &c, bool isLetter) {
        if (isLetter) {
            if (c == '0') c = 'O';
            if (c == '1') c = 'I';
            if (c == '8') c = 'B';
        } else {
            if (c == 'I' || c == 'l' || c == 'S') c = '1';
            if (c == 'O' || c == 'Q' || c == 'D') c = '0';
            if (c == 'G') c = '6';
            if (c == 'Z') c = '2';
            if (c == 'B') c = '8';
        }
    };

    for (int i = 0; i < text.length(); ++i) {
        if (i < 2 || i >= 6) fixChar(text[i], true);
        else fixChar(text[i], false);
    }
    return text;
}

// Валідація мінімального цифрового наповнення рядка
bool isGoodPlate(const string& s) {
    int digits = 0;
    for(char c : s) if(isdigit(c)) digits++;
    return digits >= 3;
}

// Перевірка відповідності тексту регулярному виразу національного стандарту ДСТУ 4278:2006
bool isValidUkrainianPlate(const string& s) {
    const regex pattern("^[A-Z]{2}[0-9]{4}[A-Z]{2}$");
    return regex_match(s, pattern);
}

int main() {
    ParkingSystem parking;
    int currentMode = 0; // Напрямок контролю: 0 - В'їзд, 1 - Виїзд

    // Менеджмент логіки резервних сесій
    bool isCarDetectedInFrame = false;
    bool promptForTicket = false;
    auto lastCarSeenTime = chrono::system_clock::now();

    // Параметри конфігурації нейромережі та мовних пакетів OCR
    string model_path = "/Users/Mac/CLionProjects/Parking/best.onnx";
    string tessdata_dir = "/Users/Mac/CLionProjects/Parking/tessdata";

    // Завантаження вагів моделі YOLOv8 детектору
    Net net;
    try {
        net = readNetFromONNX(model_path);
        cout << "YOLOv8 завантажено." << endl;
    } catch (const Exception& e) {
        cerr << "Критична помилка завантаження YOLO: " << e.what() << endl;
        return -1;
    }

    net.setPreferableBackend(DNN_BACKEND_OPENCV);
    net.setPreferableTarget(DNN_TARGET_CPU);

    // Конфігурація Tesseract OCR під роботу з однорядковими буквено-цифровими токенами
    tesseract::TessBaseAPI *ocr = new tesseract::TessBaseAPI();
    if (ocr->Init(tessdata_dir.c_str(), "eng")) {
        cerr << "Критична помилка: не знайдено файл eng.traineddata у папці tessdata!" << endl;
        return -1;
    }
    ocr->SetPageSegMode(tesseract::PSM_SINGLE_LINE);
    ocr->SetVariable("tessedit_char_whitelist", "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
    ocr->SetVariable("load_system_dawg", "F");
    ocr->SetVariable("load_freq_dawg", "F");

    // Ініціалізація потоку захоплення відеокамери
    VideoCapture cap(1);
    if (!cap.isOpened()) {
        cerr << "Критична помилка: камера недоступна." << endl;
        return -1;
    }

    Mat frame, blob;
    cout << "Система активна. [TAB] - режим, [q] - вихід, [t] - швидкий талон, [c] - взяти талон, [v] - виїзд за талоном." << endl;

    while (true) {
        cap >> frame;
        if (frame.empty()) {
            cerr << "Помилка: порожній кадр." << endl;
            break;
        }

        parking.updateGate();
        isCarDetectedInFrame = false;

        // Обробка подій клавіатури термінала
        int key = waitKey(1);
        if (key == 9) {
            currentMode = !currentMode;
            promptForTicket = false;
        }
        if (key == 't') {
            parking.registerTicketEntry();
            promptForTicket = false;
        }
        if ((key == 'c' || key == 'C') && promptForTicket && currentMode == 0) {
            cout << "[АКТ СТАНЦІЇ] Кнопка 'C' натиснута. Видаємо фізичний талон..." << endl;
            parking.registerTicketEntry();
            promptForTicket = false;
            lastCarSeenTime = chrono::system_clock::now();
        }
        if ((key == 'v' || key == 'V') && currentMode == 1 && !parking.gateOpen) {
            cout << "[АКТ СТАНЦІЇ] Кнопка 'V' натиснута. Запит на виїзд за талоном..." << endl;
            parking.processTicketExit();
        }
        if (key == 'q') break;

        if (parking.gateOpen) {
            putText(frame, "GATE OPEN - WAITING", Point(50, 100), FONT_HERSHEY_SIMPLEX, 1.2, Scalar(0, 255, 0), 3);
            promptForTicket = false;
        } else {
            float x_scale = (float)frame.cols / 640.0f;
            float y_scale = (float)frame.rows / 640.0f;

            // Конвертація та запуск логічного виводу DNN YOLOv8
            blobFromImage(frame, blob, 1.0 / 255.0, Size(640, 640), Scalar(), true, false);
            net.setInput(blob);

            vector<Mat> outputs;
            net.forward(outputs, net.getUnconnectedOutLayersNames());

            Mat out = outputs[0];
            Mat outMat(out.size[1], out.size[2], CV_32F, out.ptr<float>());
            outMat = outMat.t();

            vector<Rect> boxes;
            vector<float> confidences;

            for (int i = 0; i < outMat.rows; ++i) {
                float* row = outMat.ptr<float>(i);
                float conf = row[4];

                if (conf > 0.5) {
                    float cx = row[0] * x_scale;
                    float cy = row[1] * y_scale;
                    float w  = row[2] * x_scale;
                    float h  = row[3] * y_scale;

                    int left = int(cx - w / 2);
                    int top  = int(cy - h / 2);

                    confidences.push_back(conf);
                    boxes.push_back(Rect(left, top, int(w), int(h)));
                }
            }

            // Мінімізація накладання обмежувальних рамок детекції
            vector<int> indices;
            NMSBoxes(boxes, confidences, 0.5, 0.4, indices);

            if (!indices.empty()) {
                isCarDetectedInFrame = true;
            } else {
                lastCarSeenTime = chrono::system_clock::now();
                promptForTicket = false;
            }

            // Pipeline препроцесингу локалізованої області номерного знака
            for (int idx : indices) {
                Rect box = boxes[idx];

                box.x = max(0, box.x);
                box.y = max(0, box.y);
                box.width = min(frame.cols - box.x, box.width);
                box.height = min(frame.rows - box.y, box.height);

                rectangle(frame, box, Scalar(0, 255, 0), 2);

                Mat croppedPlate = frame(box);
                if (croppedPlate.empty()) continue;

                // Фільтрація високочастотних шумів та бінаризація методом Отсу
                Mat gray, bwPlateInverted;
                cvtColor(croppedPlate, gray, COLOR_BGR2GRAY);
                GaussianBlur(gray, gray, Size(3, 3), 0);
                threshold(gray, bwPlateInverted, 0, 255, THRESH_BINARY_INV | THRESH_OTSU);

                // Компенсація кута нахилу (Deskewing) через пошук головного контуру
                vector<vector<Point>> contours;
                findContours(bwPlateInverted, contours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);

                if (!contours.empty()) {
                    auto it = max_element(contours.begin(), contours.end(), [](const vector<Point>& a, const vector<Point>& b){
                        return contourArea(a) < contourArea(b);
                    });

                    RotatedRect minRect = minAreaRect(*it);
                    float angle = minRect.angle;

                    if (angle < -45.0f) angle += 90.0f;
                    else angle = -angle;

                    if (abs(angle) > 1.0f && abs(angle) < 40.0f) {
                        Mat rotMatrix = getRotationMatrix2D(minRect.center, angle, 1.0);
                        warpAffine(gray, gray, rotMatrix, gray.size(), INTER_CUBIC, BORDER_REPLICATE);
                    }
                }

                // Вирізання євростікера/національної символіки для запобігання помилкам OCR
                int cropWidth = int(gray.cols * 0.1);
                gray = gray(Rect(cropWidth, 0, gray.cols - cropWidth, gray.rows));

                // Геометрична інтерполяція та нормалізація гістограми (CLAHE)
                Mat resizedGray;
                resize(gray, resizedGray, Size(), 2.0, 2.0, INTER_CUBIC);
                Ptr<CLAHE> clahe = createCLAHE(2.0, Size(8, 8));
                clahe->apply(resizedGray, resizedGray);

                // Генерація трьох каналів бінаризації для паралельного аналізу в Tesseract
                vector<Mat> variants(3);
                threshold(resizedGray, variants[0], 0, 255, THRESH_BINARY | THRESH_OTSU);
                threshold(resizedGray, variants[1], 100, 255, THRESH_BINARY);
                adaptiveThreshold(resizedGray, variants[2], 255, ADAPTIVE_THRESH_GAUSSIAN_C, THRESH_BINARY, 11, 2);

                string bestText = "";
                for(auto& v : variants) {
                    ocr->SetImage(v.data, v.cols, v.rows, 1, v.step);
                    char* out = ocr->GetUTF8Text();
                    if(out) {
                        string t = "";
                        for(unsigned char* p = (unsigned char*)out; *p != '\0'; ++p) if(isalnum(*p)) t += (char)*p;
                        t = cleanPlate(t);

                        if(isValidUkrainianPlate(t)) { bestText = t; delete[] out; break; }
                        if(bestText.empty() && t.length() >= 5) bestText = t;

                        delete[] out;
                    }
                }

                // Логіка верифікації та диспетчеризації сесій за результатами OCR
                if (!bestText.empty()) {
                    if (isValidUkrainianPlate(bestText)) {
                        putText(frame, bestText, Point(box.x, box.y - 10), FONT_HERSHEY_SIMPLEX, 0.8, Scalar(0, 255, 0), 2);
                        if (parking.isStableDetection(bestText)) {
                            if (currentMode == 0) parking.registerEntry(bestText);
                            else parking.processExit(bestText);
                            lastCarSeenTime = chrono::system_clock::now();
                            promptForTicket = false;
                        }
                    } else {
                        putText(frame, "INVALID: " + bestText, Point(box.x, box.y - 30), FONT_HERSHEY_SIMPLEX, 0.6, Scalar(0, 0, 255), 2);
                    }
                }
            }

            // Перевірка часу простою транспортного засобу для активації пропозиції талона
            if (currentMode == 0 && !parking.gateOpen && isCarDetectedInFrame) {
                auto now = chrono::system_clock::now();
                auto elapsed = chrono::duration_cast<chrono::seconds>(now - lastCarSeenTime).count();

                if (elapsed > 15) {
                    promptForTicket = true;
                }
            }
        }

        // Рендеринг елементів графічного інтерфейсу користувача (GUI)
        if (promptForTicket && currentMode == 0) {
            putText(frame, "TAKE TICKET: PRESS 'C'", Point(50, 150), FONT_HERSHEY_SIMPLEX, 1.2, Scalar(0, 0, 255), 3);
        }
        if (currentMode == 1 && !parking.gateOpen) {
            putText(frame, "IF TICKET CAR: PRESS 'V'", Point(50, 150), FONT_HERSHEY_SIMPLEX, 1.0, Scalar(255, 165, 0), 2);
        }

        string modeText = (currentMode == 0) ? "MODE: ENTRY" : "MODE: EXIT";
        string info = modeText + " | PLACES: " + to_string(parking.getFreeSpaces());
        putText(frame, info, Point(50, 50), FONT_HERSHEY_SIMPLEX, 1.2, Scalar(255, 0, 0), 3);

        imshow("Smart Parking - YOLOv8 + OCR", frame);
    }

    // Звільнення дескрипторів сторонніх бібліотек та закриття вікон
    ocr->End();
    cap.release();
    destroyAllWindows();
    return 0;
}