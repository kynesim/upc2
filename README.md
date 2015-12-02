upc2
====

upc2 is a replacement for the venerable upc terminal program Kynesim
uses to boot things.  It is a combined bootloader and console with a
flexible and extensible set of protocols, and can easily handle
multi-stage boots.


Usage
-----
```
upc2 [--help] [--serial <device>] [--log <filename>] <boot-stage>* [<baud>]

  --help             Outputs a syntax help message.
  --serial <device>  Specifies the serial device to communicate with,
                     by default "/dev/ttyUSB0".
  --log <filename>   Logs output from the serial connection to the
                     named file.  Does not log console input sent to
                     the serial connection.  Protocol handshakes may
                     be omitted depending on the protocol.
  <baud>             The baud rate used for serial communications once
                     all uploads have been completed.  If omitted, a
                     baud rate of 115200 will be used.
  <boot-stage>       Stages of booting are specified by a sequence of
                     command-line arguments:
    --grouch <filename>  The file to upload.  This is the first
                         argument of any boot stage.
    --baud <baud>        The serial baud rate to use for this boot
                         stage.  The first boot stage defaults to a
                         115200 baud rate; subsequent boot stages
                         default to the baud rate of the previous
                         stage.
    --protocol <name>    The protocol to use to upload the file.  At
                         present two protocols are supported;
                         "grouch", a simple in-house protocol, and
                         "xmodem", the venerable XMODEM protocol.
```

Baud rates can be abbreviated with "k" for kilobaud and "m" for
megabaud, so 1 megabaud can be specified as "1m".

For example, imagine a device with a two-stage boot connected to
/dev/ttyUSB1.  The first stage is a file "hub.bin" sent using XModem
at 115200 baud.  The second stage is a file "nfs.cpio" sent using
grouch at 1 megabaud.  The final result should be give a serial
terminal at 115200 baud again.  The up2c command line for this is:

```
upc2 --serial /dev/ttyUSB1 \
  --grouch /path/to/hub.bin --protocol xmodem --baud 115200 \
  --grouch /path/to/nfs.cpio --protocol grouch --baud 1m \
  115200
```

Keyboard handling
-----------------

Uploads can be interrupted by the key sequence "C-a x", which will
cause upc2 to quit.

During uploads, input from the console is not passed through to the
serial connection.  Once the uploads are complete, the serial
connection is switched to the specified baud rate and upc2 becomes a
console program.  Commands are given to the program through escape key
sequences beginning with a C-a:

 *  `C-a x`   Quits upc2
 *  `C-a h`   Prints a console help message
 *  `C-a C-a` Sends a C-a through the serial connection

Other escape sequences may be added as needed, so users should not
expect "C-a <key>" to send <key> to the serial connection without
checking.  Traps have been laid for the excessively bold.


<rrw@kynesim.co.uk>
2015-12-01
<rhodri@Kynesim.co.uk>
2015-12-02
