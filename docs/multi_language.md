# Openterface Mini-KVM Multi-Language Guide

## **I. Introduction**

Welcome to the **Openterface Mini-KVM** multi-language guide. This document explains how to add a new language to OpenterfaceQT.

## **II. UnderStand file structure**

Before starting, familiarize yourself with the project's file structure. The key files for adding a new language are located within the `config/languages` directory and the project's root directory.

```
Openterface_QT/
├── config/languages/
│   ├── openterface_en.ts
│   ├── openterface_fr.ts
│   ├── openterface_da.ts
│   ├── openterface_ja.ts
│   ├── openterface_se.ts
│   ├── openterface_de.ts
│   ├──languages.qrc
│   └── ...
├── openterface.pro
└── ...
```

**Key Files:**

* `.ts` files (e.g., `openterface_en.ts`): These files in the `config/languages` directory contain the translatable text for each language.
* `languages.qrc`: This resource file, located in `config/languages`, lists the compiled language files (`.qm`).
* `openterface.pro`: This project file in the root directory needs to be updated to include the new `.ts` file.

## **III. Steps to Add a New Language**

Follow these steps to integrate a new language into OpenterfaceQT:

**1. Modify the `.pro` File:**

* Open the `openterface.pro` file.
* Locate the `TRANSLATIONS` variable.
* Add the path to the new language's `.ts` file to this variable.

    ```pro
    TRANSLATIONS += config/languages/openterface_en.ts \
                    config/languages/openterface_fr.ts \
                    config/languages/openterface_da.ts \
                    config/languages/openterface_ja.ts \
                    config/languages/openterface_se.ts \
                    config/languages/openterface_de.ts \
                    config/languages/openterface_**<new_language_code>**.ts # Add your new language here
    ```

* **Example:** If you are adding a Chinese translation, the line would look like:

    ```pro
    TRANSLATIONS += ... \
                    config/languages/openterface_zh.ts
    ```

**2. Generate the `.ts` File:**

* Open your terminal or command prompt.
* Navigate to the root directory of the `Openterface_QT` project.
* Run the `lupdate` command with the project file:

    ```sh
    lupdate openterface.pro
    ```

    This command will scan your project files and generate (or update) the `.ts` file for your new language (e.g., `openterface_zh.ts`) in the `config/languages` directory.

**3. Translate the `.ts` File:**

* Open the newly generated `.ts` file (e.g., `config/languages/openterface_zh.ts`) using a text editor or a dedicated Qt translation tool (like Qt Linguist).
* Translate the `<source>` text to the target language within the `<translation>` tags.

    ```xml
    <context>
        <message>
            <location filename="openterface_zh.ts" line="1"/>
            <source>hello</source>
            <translation>你好</translation>
        </message>
    </context>
    ```

**4. Compile the `.ts` File:**

* Open your terminal or command prompt again.
* Navigate to the root directory of the `Openterface_QT` project.
* Run the `lrelease` command with the project file:

    ```sh
    lrelease openterface.pro
    ```

    This command will compile the translated `.ts` file into a `.qm` file (e.g., `openterface_zh.qm`) in the same directory (`config/languages`). The `.qm` file is the optimized binary format used by Qt for translations.

**5. Modify the `languages.qrc` File:**

* Open the `config/languages/languages.qrc` file.
* Add a `<file>` tag for the newly compiled `.qm` file within the `<qresource>` section.

    ```xml
    <!DOCTYPE RCC>
    <RCC version="1.0">
        <qresource prefix="/config/languages">
            <file>openterface_da.qm</file>
            <file>openterface_de.qm</file>
            <file>openterface_en.qm</file>
            <file>openterface_fr.qm</file>
            <file>openterface_ja.qm</file>
            <file>openterface_se.qm</file>
            <file>openterface_**<new_language_code>**.qm</file> </qresource>
    </RCC>
    ```

* **Example:** For the Chinese translation, add:

    ```xml
            <file>openterface_zh.qm</file>
    ```

**6. Compile the Project:**

* Finally, rebuild your OpenterfaceQT project using your preferred build system (e.g., `qmake` followed by `make` or using Qt Creator's build functionality).

After successful compilation, the new language should be available in the language menu of your Openterface Mini-KVM application.