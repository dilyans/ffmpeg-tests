# Description
Extracts keyframes from a given video file. The keyframes are then  convert to grayscale and split in a grid with specified dimensions. The median  value of all the pixels in each grid cell together with a timestamp of the keyframe are written to a CSV file. The file is based on ffmpeg example files and it should be able to work with different types of vide formats/codecs - mp4, mpeg1, mpeg2, gp3 etc. 

# Compliation
The program requires ffmpeg 4 to work properly. It was tested on ubuntu  18.04 and MacOS Mojave. Since ffmpeg is available on Windows it should be possible to run on Windows too, however it was not tested.
On ubuntu ffmpeg version needs to be updated to 4.X. For more information on this [link](http://ubuntuhandbook.org/index.php/2018/10/install-ffmpeg-4-0-2-ubuntu-18-0416-04/)
Also the *-dev packages part of the ffmpeg release need to be installed for the program to be compiled: [link](https://askubuntu.com/questions/766606/where-are-the-headers-of-libavcodec-and-libavformat-located?rq=1)
On MacOS ffmpeg can be installed via  [brew](https://trac.ffmpeg.org/wiki/CompilationGuide/macOS).
The Makefile also uses the pkg-config to identify the correct location of headers and libraries for the local ffmpeg version
```
make -f Makefile
```
# Running
```
./key_frame_grid_mean input_vide_file output_cvs_file grid_widthxgrid_height
```
**input_vide_file** is a valid file containing vide data.

**output_cvs_file is name** of output file where frame data will be written.

**grid_widthxgrid_height** is the width and height of grind for example 3x3

# Notes
Currently keyframes are detected from the decompressed frame AVFrame structure, it looks like that the AVPacket also has a flag that indicates if a packet is keyframe. In that case the program can be modified to run much faster since it will have to decompress only packets that are keyframes instead of all packets as currently implemented.

Also as currently implemented the grid might not containg 100% of the image. Since integer division is used to calculate the width of each grid cell in pixels (image_width/cell_columns), the reminder of the division will be a number smaller that the cell width in pixels and will not be part of the medean calculations. The same is valid for the last few rows of the image. This can be improved relavley easy and will happen only if image_width/cell_columns is not a whole number.  
