import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
import time
import struct
import sys
import os
import argparse
import math
import cv2
from scipy.spatial import Delaunay
np.set_printoptions(threshold=np.inf)



deci_ratio = 1024
if deci_ratio == 16:
    data_len = 2808050
elif deci_ratio == 1024:
    data_len = 43876

def get_arguments():
    parser = argparse.ArgumentParser(description='Blob Detection and Plotting Script')
    parser.add_argument('--step', type=str, default='0', help="Step number.")
    return parser.parse_args()

args = get_arguments()
step = args.step
reduced_filename = f"/home/cc/zmqClient/data/reduced/{step}/reduced_data_xgc_16.bin"
delta_path = f"/home/cc/zmqClient/data/delta/{step}/"

orig_output_name = f"../data/analysis/{step}/unblobed_t.png"
blob_output_name = f'../data/analysis/{step}/blobed_t.pdf'
output_dir_orig = os.path.dirname(orig_output_name)
output_dir_blob = os.path.dirname(blob_output_name)

os.makedirs(output_dir_orig, exist_ok=True)
os.makedirs(output_dir_blob, exist_ok=True)

# Read reduced data
f = open(reduced_filename, "rb")
data_str = f.read(data_len*8)
r_str = f.read(data_len*8)
z_str = f.read(data_len*8)
f.close()
data = struct.unpack(str(data_len)+'d', data_str)
r = struct.unpack(str(data_len)+'d', r_str)
z = struct.unpack(str(data_len)+'d', z_str)

print(f"step {step}: Reduced length:", len(data))

# Read augmentation data
f_d = delta_path + "delta_xgc_o.bin"
f_r = delta_path + "delta_r_xgc_o.bin"
f_z = delta_path + "delta_z_xgc_o.bin"
delta_len = 44928785

f = open(f_d, "rb")
data_str = f.read(delta_len*8)
f.close()

f = open(f_r, "rb")
r_str = f.read(delta_len*8)
f.close()  

f = open(f_z, "rb")
z_str = f.read(delta_len*8)
f.close()

dn = (struct.unpack(str(delta_len)+'d', data_str))
rn = (struct.unpack(str(delta_len)+'d', r_str))
zn = (struct.unpack(str(delta_len)+'d', z_str))
data = data + dn
r = r + rn
z = z + zn

print(f"step {step}: New len:", len(data))

# Plot data to png
def plot(data, r, z):
    points = np.transpose(np.array([z, r]))
    Delaunay_t = Delaunay(points)
    conn = Delaunay_t.simplices
    fig, ax = plt.subplots(figsize=(8, 8))
    plt.rc('xtick', labelsize=26)
    plt.rc('ytick', labelsize=26)
    axis_font = {'fontname':'Arial', 'size':'38'}
    plt.tricontourf(r, z, conn, data, cmap=plt.cm.jet,
        levels=np.linspace(np.min(data), np.max(data), num=25))
    plt.xticks([])
    plt.yticks([])
    ax.margins(x=0, y=0)
    for key, spine in ax.spines.items():
        if key == 'right' or key == 'top' or key == 'left' or key == 'bottom':
            spine.set_visible(False)
    plt.subplots_adjust(left=0, right=1, top=1, bottom=0)
    plt.savefig(orig_output_name, dpi=100, format='png')

print(f"step {step}: Start plotting")
start = time.time()
plot(data, r, z)
end = time.time()
print(f"step {step}: Plot time =", end-start)


def pdist(pt1, pt2):
    x = pt1[0] - pt2[0]
    y = pt1[1] - pt2[1]
    return math.sqrt(math.pow(x, 2) + math.pow(y, 2))

def blob_detection(fname):
    image = cv2.imread(fname)
    height, width, channels = image.shape
    print(f"step {step}: Image H =", height, ", W =", width)
    boundaries = [
        ([0, 0, 100], [204, 204, 255]), #red 
        ([86, 31, 4], [220, 88, 50]),
        ([25, 146, 190], [62, 174, 250]),
        ([103, 86, 65], [145, 133, 128])
    ]

    (lower, upper) = boundaries[0]

    lower = np.array(lower, dtype = "uint8")
    upper = np.array(upper, dtype = "uint8")
    params = cv2.SimpleBlobDetector_Params()

    # Change thresholds
    params.minThreshold = 10
    params.maxThreshold = 200

    # Filter by Area
    params.filterByArea = True
    params.minArea = 120

    # Filter by Circularity
    #params.filterByCircularity = True
    #params.minCircularity = 0.1

    # Filter by Convexity
    params.filterByConvexity = True
    params.minConvexity = 0.1
    #params.maxConvexity = 1


    # Filter by Inertia
    params.filterByInertia = True
    params.minInertiaRatio = 0.1
    #params.maxInertiaRatio = 1

    # Set up the detector with default parameters.
    detector = cv2.SimpleBlobDetector_create(params)

    keypoints = []    
    i = 0
    time1 = time.time()
    mask = cv2.inRange(image, lower, upper)
    output = cv2.bitwise_and(image, image, mask = mask)
    gray_image = cv2.cvtColor(output, cv2.COLOR_BGR2GRAY)

    keypoints = detector.detect(gray_image) # keypoints are blobs
    time2 = time.time()
    print(f'step {step}: blob detection time', (time2 - time1))
    print(f'step {step}: blob # %d' %(len(keypoints)))

    total_diameter = 0
    total_blob_area = 0
    for k in keypoints:
        print(f"step {step}: Location =", k.pt, "Size =", k.size)
        total_diameter = total_diameter + k.size
        total_blob_area = total_blob_area + 3.14 * math.pow(k.size/2, 2)
    if len(keypoints):
        print(f'step {step}: avg diameter', total_diameter / len(keypoints))
    else:
        print('ERROR: avg diameter', 0)
    print(f'step {step}: aggregate blob area', total_blob_area)

    overlap = 0
    for k in keypoints:
        for p in keypoints:
            if pdist(k.pt, p.pt) < (k.size + p.size) * 1.0 / 2.0:
                overlap = overlap + 1.0
                break
    if len(keypoints):
        print(f'step {step}: overlap ratio', overlap / len(keypoints))
    else:
        print('ERROR: overlap ratio', 0)
        
    im_with_keypoints = cv2.drawKeypoints(
        cv2.cvtColor(image, cv2.COLOR_BGR2RGB),
        keypoints, np.array([]), (0, 0, 255),
        cv2.DRAW_MATCHES_FLAGS_DRAW_RICH_KEYPOINTS)

    plt.imshow(im_with_keypoints)
    plt.axis('off')

    blob_number = len(keypoints)
    if blob_number > 0:
        blob_diameter = total_diameter / blob_number
        overlap_ratio = overlap / blob_number
    else:
        blob_diameter = 0
        overlap_ratio = 0
    blob_area = total_blob_area
    print(f"step {step}: blob_number ={blob_number}")
    print(f"step {step}: blob_diameter ={blob_diameter}")
    print(f"step {step}: blob_area ={blob_area}")
    print(f"step {step}: overlap_ratio ={overlap_ratio}")
    plt.savefig(blob_output_name, dpi=600, format='pdf')

    return keypoints


blobs = blob_detection(orig_output_name)