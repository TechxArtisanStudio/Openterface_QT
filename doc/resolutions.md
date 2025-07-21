# Openterface Mini-KVM Resolution Guide

## **I. Introduction**

Welcome to the guide for understanding the resolution capabilities of your **Openterface Mini-KVM**. The Openterface Mini-KVM is a **USB KVM solution** that allows you to control computers with **high-quality video** output. This device supports resolutions up to **4K**, enabling detailed and clear visuals for a variety of tasks. This section will guide you through the **supported resolutions** of the Mini-KVM and provide you with valuable **tips for achieving the best possible visual experience** when controlling your target computers. Understanding how the Mini-KVM handles different resolutions is key to optimizing its performance for your specific needs.

## **II. Understanding Key Resolution Terms**  
Before we delve into the specific resolutions supported by your Openterface Mini-KVM, it's important to understand a few key terms:

* **Input Resolution**: This refers to the **resolution of the video signal that the Openterface Mini-KVM receives from the target computer**. Think of it as the **output resolution set within the target computer's operating system**. It's crucial to note that the **input resolution is controlled by the settings of the target operating system and not by the Openterface software**.  
* **Capture Resolution**: This refers to the **resolution at which the Openterface Mini-KVM hardware captures and processes the video signal from the target computer for display on your host computer**. Based on the information regarding optimal display quality and handling higher input resolutions, the **highest effective capture resolution for the Openterface Mini-KVM is 1920x1080**. While the device can accept higher input resolutions, it will process them within its capture capabilities.

Now that we have a clear understanding of these terms, let's look at the specific standard input resolutions supported by your Openterface Mini-KVM.

## **III. Supported Standard Input Resolutions**  
The Openterface Mini-KVM can receive and process a range of **standard input resolutions** from your target computer. These supported resolutions are generally **higher than 1920x1080**:

* **4096 x 2160 @ 30Hz, 29.97Hz** (Highest resolution supported by the Openterface Mini-KVM)  
* **3840 x 2160 @ 30Hz, 29.97Hz** (**4k resolution with 16:9 aspect ratio**)  
* **2100 x 1050 @ 60Hz** (**optimized for mobile devices with 16:9 aspect ratio**)  
* **1920 x 1200 @ 60Hz** (**16:10 aspect ratio**) 
* **1920 x 1080 @ 60Hz** (**The best display resolution with 16:9 aspect ratio**)
* **1680 x 1050 @ 60Hz** (**16:9 aspect ratio**)  
* **1600 x 900 @ 60Hz** (**16:9 aspect ratio**)  
* **1440 x 900 @ 60Hz** (**16:9 aspect ratio**)  
* **1280 x 1024 @ 60Hz** (**5:4 aspect ratio**)  
* **1280 x 960 @ 60Hz** (**4:3 aspect ratio**)  
* **1280 x 800 @ 60Hz** (**16:10 aspect ratio**)  
* **1280 x 720 @ 60Hz** (**16:9 aspect ratio**)  
* **1152 x 864 @ 60Hz** (**4:3 aspect ratio**)  
* **1024 x 768 @ 60Hz** (**Optimized for old CRT monitors resolution with 4:3 aspect ratio**)  
* **800 x 600 @ 60Hz** (**  Optimized for old CRT monitors resolution with 4:3 aspect ratio**)  
* **640 x 480 @ 60Hz** (**Optimized for old CRT monitors resolution with 4:3 aspect ratio**)  

These are the standard resolutions that the Openterface Mini-KVM is designed to handle as input from the connected computer

## **IV. Implications of Resolutions Higher Than Capture Resolution**

It's important to understand what happens when the **input resolution from your target computer is set higher than the Openterface Mini-KVM's capture resolution**. Based on our understanding, the **implicit best capture resolution of the Mini-KVM is 1920x1080**.

When the **input resolution exceeds this capture resolution**, the Openterface Mini-KVM needs to process a larger number of pixels than it can display directly at its capture resolution. This results in **multiple pixels from the target system's video output being merged into a single pixel on the host computer's display**.

The primary consequence of this pixel merging is that the **displayed video image on your host computer may appear blurry**. This is because the fine details present in the higher input resolution are being compressed and averaged into fewer pixels on the host display.

To potentially improve the visual quality in scenarios where the input resolution is higher than the capture resolution, it is recommended to **scale up the pixels on your host system**. While this doesn't restore the original level of detail, it can create a **better visual experience** because when the system scales up the image, it often leverages the similarity between nearby pixels to produce a smoother output. However, it's crucial to remember that the underlying capture resolution remains 1920x1080, so the image will not have the same clarity as if the input resolution matched the capture resolution.

## **V. Troubleshooting Display Issues**

Occasionally, you might encounter display issues such as **video glitching** after configuring a high resolution or refresh rate on your target computer. This can happen if the selected settings are not optimally supported or exceed the stable operating range of the capture card chip.

If you experience such display problems, here is a recommended recovery step:

* Temporarily **set the capture resolution within the Openterface software to a lower value: 640 x 480 @ 30Hz**.

The reason this step is effective is that **lower resolutions require fewer processing resources**. By reducing the demand on the Mini-KVM hardware, you should be able to regain a stable display output.

Once you have a stable display at this lower resolution, you can then **revert the problematic resolution or refresh rate settings on your target computer's operating system**. After making these changes on the target system, you can then attempt to set a higher resolution within the Openterface software again, preferably one of the **supported standard input resolutions**.

Remember that for **non-standard resolutions**, it is generally advisable to **keep the refresh rate below 60 frames per second** to minimize the likelihood of encountering display glitches.

## **VII. Tips for Optimal Display Quality**

To achieve the best possible visual experience when using your Openterface Mini-KVM, consider the following tips:

* **Aim for Matching Resolutions:** The **best display quality is generally achieved when both the input resolution of the target computer and the capture resolution of the USB KVM are set to 1920x1080**. This avoids any pixel merging or scaling that can introduce blurriness.

* **Be Mindful of Higher Input Resolutions:** If you need to use an input resolution higher than 1920x1080, be aware that the image on your host computer might appear somewhat blurry due to pixel merging. In such cases, **scaling up the pixels on your host system can sometimes improve the visual quality**.

## **VIII. Conclusion**

In conclusion, the **Openterface Mini-KVM** is a versatile **USB KVM solution** capable of supporting **high video resolutions up to 4K**. By providing a direct connection to your headless or unattended computers, it offers a convenient way to manage and control them with clear and detailed visuals.

To ensure the **best possible visual experience**, it is important to be mindful of the **resolution settings on your target operating system**. Understanding the relationship between the **input resolution from your target computer** and the **Openterface Mini-KVM's capture capabilities (with an effective highest resolution of 1920x1080)** will help you optimize your setup.

By adhering to the tips provided in this guide, such as **aiming for an input resolution of 1920x1080 for optimal clarity**, exercising caution with non-standard resolutions and refresh rates, and utilizing the troubleshooting steps when necessary, you can effectively leverage the high-resolution support of your Openterface Mini-KVM for a seamless and efficient control experience

