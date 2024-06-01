Copy this .deb file to your sBitx and invoke the command: dpkg -i freedv_ptt.deb 

(dpkg -i --force-all freedv_ptt.deb to reinstall)

This .deb file will install the required codec2 library (libcodec2.so.1.2) under /usr/lib/ It will also place the TXRX codecs as well as the control software into the folder /home/pi/freedv_ptt 

Start the sBitx radio software and allow it to display the waterfall
Invoke the application with the FreeDV_PTT desktop icon.

Please make sure to click the settings button and enter your callsign before transmitting.
