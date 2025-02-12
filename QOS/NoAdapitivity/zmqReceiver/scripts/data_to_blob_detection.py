import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
import time
import struct
import argparse
import math
import cv2
from scipy.spatial import Delaunay

np.set_printoptions(threshold=np.inf)

def get_arguments():
    parser = argparse.ArgumentParser(description='Blob Detection and Plotting Script')
    parser.add_argument('--app_name', type=str, default='xgc', choices=['xgc', 'astro', 'cfd'],
                        help="Application name. Choose from 'xgc', 'astro', or 'cfd'. Default is 'xgc'.")
    parser.add_argument('--path', type=str, default='/app/',
                        help="Path of your .bin file. Default is '/app/'.")
    parser.add_argument('--data_type', type=str, default='reduced', choices=['full', 'reduced'],
                        help="Data type to use: 'full' or 'reduced'. Default is 'reduced'.")
    parser.add_argument('--output_name', type=str, default="reduced.png",
                        help="Output filename for the plot. Default is 'xgc.png'.")
    parser.add_argument('--step', type=str, default='0', help="Step number.")
    return parser.parse_args()

def plot(data, r, z, filename):
    points = np.transpose(np.array([z, r]))
    Delaunay_t = Delaunay(points)
    conn = Delaunay_t.simplices
    fig, ax = plt.subplots(figsize=(8, 8))
    plt.rc('xtick', labelsize=26)
    plt.rc('ytick', labelsize=26)
    axis_font = {'fontname': 'Arial', 'size': '38'}
    plt.tricontourf(r, z, conn, data, cmap=plt.cm.jet,
                    levels=np.linspace(np.min(data), np.max(data), num=25))
    plt.xticks([])
    plt.yticks([])
    for key, spine in ax.spines.items():
        if key == 'right' or key == 'top' or key == 'left' or key == 'bottom':
            spine.set_visible(False)
    plt.savefig(filename, format='png')

def pdist(pt1, pt2):
    x = pt1[0] - pt2[0]
    y = pt1[1] - pt2[1]
    return math.sqrt(math.pow(x, 2) + math.pow(y, 2))

def blob_detection(fname, dataType, step):
    image = []
    image.append(cv2.imread(fname))
    boundaries = [
        ([0, 0, 100], [204, 204, 255]),  # red
        ([86, 31, 4], [220, 88, 50]),
        ([25, 146, 190], [62, 174, 250]),
        ([103, 86, 65], [145, 133, 128])
    ]
    
    (lower, upper) = boundaries[0]
    lower = np.array(lower, dtype="uint8")
    upper = np.array(upper, dtype="uint8")
    params = cv2.SimpleBlobDetector_Params()
    params.minThreshold = 10
    params.maxThreshold = 200
    params.filterByArea = 1
    params.minArea = 120
    params.filterByConvexity = 1
    params.minConvexity = 0.3
    params.filterByInertia = 1
    params.minInertiaRatio = 0.1
    detector = cv2.SimpleBlobDetector_create(params)

    mask = cv2.inRange(image[0], lower, upper)
    output = cv2.bitwise_and(image[0], image[0], mask=mask)
    gray_image = cv2.cvtColor(output, cv2.COLOR_BGR2GRAY)
    keypoints = detector.detect(gray_image)
    
    total_diameter = sum(k.size for k in keypoints)
    total_blob_area = sum(3.14 * math.pow(k.size/2, 2) for k in keypoints)
    
    if keypoints:
        print(f'step {step}:avg diameter', total_diameter / len(keypoints))
    else:
        print('ERROR: avg diameter', 0)
    print(f'step {step}: aggregate blob area', total_blob_area)

    im_with_keypoints = cv2.drawKeypoints(cv2.cvtColor(image[0], cv2.COLOR_BGR2RGB), keypoints, np.array([]), (0, 0, 255), cv2.DRAW_MATCHES_FLAGS_DRAW_RICH_KEYPOINTS)
    plt.imshow(im_with_keypoints)
    plt.axis('off')
    plt.savefig(f"blobed_{dataType}_"+ step + ".pdf", dpi=600, format='pdf')


def main():
    args = get_arguments()

    app_name = args.app_name
    path = args.path
    data_type = args.data_type
    output_name = args.output_name
    step = args.step

    if app_name == "xgc":
        fulldata_len = 44928785
        reduced_len = 2808050
    elif app_name == "astro":
        fulldata_len = 47403736
        reduced_len = 2962734
    elif app_name == "cfd":
        fulldata_len = 30764603
        reduced_len = 1922788

    if data_type == 'full':
        filename = f"{path}/{data_type}/{step}/full_data_{app_name}.bin"
        data_len = fulldata_len
    elif data_type == 'reduced':
        filename = f"{path}/{data_type}/{step}reduced_data_{app_name}_16_{step}.bin"
        data_len = reduced_len

    with open(filename, "rb") as f:
        data_str = f.read(data_len * 8)
        r_str = f.read(data_len * 8)
        z_str = f.read(data_len * 8)

    data = struct.unpack(str(data_len) + 'd', data_str)
    r = struct.unpack(str(data_len) + 'd', r_str)
    z = struct.unpack(str(data_len) + 'd', z_str)

    start = time.time()
    plot(data, r, z, output_name)
    end = time.time()
    print(f"step {step}: Plot time = ", end - start)

    blob_detection(output_name, data_type, step)


if __name__ == '__main__':
    main()
