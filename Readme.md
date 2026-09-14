# Smart Parking: Automated License Plate Recognition (ALPR) System

A real-time intelligent parking management and billing system prototype implemented in C++ using OpenCV DNN, YOLOv8, and Tesseract OCR. The application automates vehicle access control, plate validation against national standards (DSTU 4278:2006), session duration tracking, and dynamic fee calculation with fallback ticketing mechanisms.

---

## Architecture & Technical Highlights

* **Deep Learning Inference (`cv::dnn::Net`):**
  Loads an ONNX-exported YOLOv8 model for high-precision license plate bounding box localization. Implements Non-Maximum Suppression (NMS) with configurable score and IOU thresholds to eliminate redundant candidate proposals.
* **Multi-Stage Preprocessing & Deskewing Pipeline:**
  * **Otsu's Binarization & Gaussian Blur:** Suppresses high-frequency noise and highlights alphanumeric contours.
  * **Affine Deskewing:** Computes the minimum area bounding rectangle of the primary contour to detect skew angles ($\pm 40^\circ$) and applies a 2D affine transformation to align the plate horizontally.
  * **Feature Isolation:** Crops edge regions (EU / national flag strips) to prevent recognition hallucinations.
  * **CLAHE & Multi-Channel Thresholding:** Applies Contrast Limited Adaptive Histogram Equalization, evaluating three concurrent thresholding passes (Otsu, Fixed Binary, Gaussian Adaptive) in Tesseract OCR to maximize character accuracy under varying illumination.
* **Error Correction & Heuristic Normalization:**
  * **Positional Syntax Correction (`cleanPlate`):** Enforces position-dependent alphanumeric conversions (e.g., distinguishing '0' vs 'O', '1' vs 'I'/'S', '8' vs 'B') based on standard Ukrainian plate templates.
  * **Approximate String Matching:** Utilizes the Levenshtein edit distance metric ($\le 1$) to match noisy exit reads against registered entry sessions.
  * **Regex Validation:** Matches plates against `^[A-Z]{2}[0-9]{4}[A-Z]{2}$`.
* **State & Billing Management (`ParkingSystem`):**
  * Tracks real-time parking occupancy, timestamps, and automated barrier gate actuators.
  * Implements frame detection debounce buffers and cooldown intervals to prevent duplicate trigger events.
  * Tiered billing engine: 60-minute complimentary window followed by dynamic hourly calculation.
  * Integrated fallback manual ticket issuance and redemption pipelines.

---

## Keyboard Controls

| Key | Action | Description |
| :--- | :--- | :--- |
| **`[TAB]`** | Toggle Gate Mode | Switches between Entry (`MODE: ENTRY`) and Exit (`MODE: EXIT`) lanes. |
| **`[T]`** | Fast Ticket Entry | Forces immediate guest ticket generation and lifts the barrier. |
| **`[C]`** | Issue Ticket | Claims a physical fallback ticket when vehicle detection prompt is active. |
| **`[V]`** | Ticket Exit | Processes exit validation for the oldest active ticket session. |
| **`[Q]`** | Exit Application | Gracefully releases camera, OCR, and GUI handles. |

---

## Build & Dependencies

### Prerequisites
* C++17 compliant compiler (GCC, Clang, or MSVC)
* CMake 3.20 or newer
* OpenCV 4.x with `dnn`, `imgproc`, `highgui`, and `videoio` modules
* Tesseract OCR runtime and development headers (`libtesseract`, `libleptonica`)
* `eng.traineddata` language model placed in your target `tessdata` directory

### Installing Dependencies

**macOS (Homebrew):**
```bash
brew install cmake opencv tesseract
```

**Ubuntu / Debian:**
```bash
sudo apt-get update && sudo apt-get install -y cmake g++ libopencv-dev libtesseract-dev tesseract-ocr-eng
```

### Build & Run

```bash
git clone [https://github.com/demchenko-eg/SmartParking.git](https://github.com/demchenko-eg/SmartParking.git)
cd SmartParking
mkdir build && cd build
cmake ..
cmake --build . --config Release
./SmartParking
```