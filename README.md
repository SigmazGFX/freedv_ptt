
FreeDV_PTT 2.4.6 for 64bit sBitx:

This is  a Codec2/FreeDV control application for use with the sBitx hybrid SDR transceiver running an updated 64bit OS release.

This **WILL NOT** run on the older 32bit OS releases.

To install,
clone this repo to your /home/pi folder with:

cd ~

git clone https://github.com/SigmazGFX/freedv_ptt <- note the case of the letters.. this matters..

cd ~/freedv_ptt

Run the command: sudo dpkg -i freedv_ptt.deb

This .deb file will install the required codec2 library and control components.

Updates can be done easily with ./update.sh located in the ~/freedv_ptt folder.
If you have already been running 2.45 please delete the config.ini file from your freedv_ptt folder, a new format file will be created.
1. Plug in your USB headset
2. Start the sBitx radio software and allow it to display the waterfall 
3. Invoke the application with the FreeDV_PTT desktop icon.

Also, Be sure to click the settings button and enter your callsign before transmitting.

![alt text](https://github.com/SigmazGFX/FreeDV_PTT/blob/main/245screencap.png)

Note: 
Occasionally the sBitx will refuse to respond to the application.

This usually happens if you close and re-open FreeDV_PTT more than a few times.

There is a small issue with the Hamlib net implementation and while I do have a way around this, It requires an update to the sbitx_gtk.c file.

If the radio fails to respond to a TX command, Stop both FreeDV_PTT and sBitx and then repeat the above steps 2 & 3.


...I like free, But what is FreeDV? 

To learn more about this amazing mode check here =>  https://freedv.org/
![image](https://github.com/SigmazGFX/FreeDV_PTT/assets/4202780/4cff3b30-e3de-4331-9e91-5adac49e4e6c)
