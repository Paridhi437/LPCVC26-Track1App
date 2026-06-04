LPCVC 2026 Track 1 Demo App

This repository contains the demonstration application for the Track 1 model of the 2026 IEEE Low-Power Computer Vision Challenge (LPCVC) running on physical mobile edge devices.



The goal of this app is to retrieve the most relevant text descriptions for a given image from the candidate pool. The system has been validated on Android 16 and Samsung Galaxy S25 \& S26 hardware.



🚀 Getting Started

You can set up the application using one of the following two paths:



Option 1: Direct APK Installation (Recommended for quick testing)

Download the compiled \[release APK].

Install the APK onto your supported Android device.

If installation is blocked, enable:

Settings → Security and privacy → Install unknown apps

Select your browser or file manager application

Enable Allow from this source

Option 2: Build from Source

If you wish to modify the application, clone this repository and build the TRACK1APP\_android module locally.



Prerequisites

Qualcomm AI Engine Direct SDK (QAIRT): Before opening the project in Android Studio, please follow the official environment setup guides found in the Qualcomm Neural Processing SDK Documentation.

Path Configuration: Define your local QAIRT path inside build.gradle where indicated:

&#x20;  def qnnSDKLocalPath= System.getenv("QAIRT\_SDK\_ROOT") ?: "C:\\\\REPLACE\\\\HERE\\\\qairt\\\\2.46.0.260424"

🛠️ App Functions \& Modes

Upon application startup, the text encoder calculates text embeddings for all pre-tokenized texts within dataset.json. This initialization is performed only once since the text dataset remains static.



Once inside the app, you can utilize three main features:



Select Image

Select an image from the device gallery.

Tap Run Model.

The application will display the ranked Top N results alongside their corresponding Cosine Similarity Scores.

Take Photo



Capture a live photo using the device camera.

Tap Run Model.

The application will display the ranked Top N results alongside their corresponding Cosine Similarity Scores.

Web Dashboard (kebab icon)



Displays a filtered feed of all images within a selected directory that match a candidate text description with a cosine similarity $> 0.25$.

Once the folder path is selected and "Run Model On Source" is clicked, the dashboard renders a clean interface linking each image to its highest-scoring text match.

📂 Project Assets \& Modification

The app comes pre-packaged with baseline assets configured out-of-the-box using tools adapted from the official lpcvai/26LPCVC\_Track1\_Sample\_Solution



📄 assets/data/dataset.json



The text candidate pool. Contains processed text:tokenized text pairs from the official Track 1 sample dataset, augmented with generic descriptions spanning several categories (e.g., food, apparel, objects, landscapes).

Tip: Use tokenize\_text2json.py to generate your own dataset.json from a custom CSV of texts.

🤖 assets/models/image\_encoder.dlc \& assets/models/text\_encoder.dlc



The image and text encoder Deep Learning Container (DLC) format models optimized from ONNX for Snapdragon execution. The included models are derived from the 1st-place winning solution found in EfficientAI's repo

📱 App Demonstration

Watch the application in action: \[Demo video]



📚 References

\[1] IEEE Low Power Computer Vision Challenge Organizing Committee. IEEE Low Power Computer Vision Challenge. Annual competition series on low power computer vision. Available: https://lpcv.ai/

\[release APK]: (THE RELEASE LINK HERE)



\[Demo video]: (THE VIDEO LINK HERE)

