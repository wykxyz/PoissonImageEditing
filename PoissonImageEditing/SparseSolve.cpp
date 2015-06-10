﻿#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

struct IndexedValue
{
    IndexedValue() : index(-1), value(0) {}
    IndexedValue(int index_, double value_) : index(index_), value(value_) {}
    int index;
    double value;
};

struct SparseMat
{
    SparseMat() : rows(0), maxCols(0) {}
    SparseMat(int rows_, int cols_) :
        rows(0), maxCols(0)
    {
        create(rows_, cols_);
    }
    void create(int rows_, int cols_)
    {
        if ((rows_ < 0) || (cols_ < 0))
            return;

        rows = rows_;
        maxCols = cols_;
        buf.resize(rows * maxCols);
        data = &buf[0];
        memset(data, -1, rows * maxCols * sizeof(IndexedValue));
        count.resize(rows);
        memset(&count[0], 0, rows * sizeof(int));
    }
    void release()
    {
        rows = 0;
        maxCols = 0;
        buf.clear();
        count.clear();
        data = 0;
    }
    const IndexedValue* rowPtr(int row) const
    {
        return ((row < 0) || (row >= rows)) ? 0 : (data + row * maxCols);
    }
    IndexedValue* rowPtr(int row)
    {
        return ((row < 0) || (row >= rows)) ? 0 : (data + row * maxCols);
    }
    void insert(int row, int index, double value)
    {
        if ((row < 0) || (row >= rows))
            return;

        int currCount = count[row];
        if (currCount == maxCols)
            return;

        IndexedValue* rowData = rowPtr(row);        
        int i = 0;
        if ((currCount > 0) && (index > rowData[0].index))
        {
            for (i = 1; i < currCount; i++)
            {
                if ((index > rowData[i - 1].index) &&
                    (index < rowData[i].index))
                    break;
            }
        }
        if (i < currCount)
        {
            for (int j = currCount; j >= i; j--)
                rowData[j + 1] = rowData[j];
        }
        rowData[i] = IndexedValue(index, value);
        ++count[row];
    }
    void calcSplit(std::vector<int>& split) const
    {
        split.resize(rows, -1);
        for (int i = 0; i < rows; i++)
        {
            const IndexedValue* ptrRow = rowPtr(i);
            for (int j = 0; j < count[i]; j++)
            {
                if (ptrRow[j].index == i)
                {
                    split[i] = j;
                    break;
                }
            }
        }
    }
    int rows, maxCols;    
    std::vector<IndexedValue> buf;
    std::vector<int> count;
    IndexedValue* data;

private:
    SparseMat(const SparseMat&);
    SparseMat& operator=(const SparseMat&);
};

void solve(const IndexedValue* A, const int* length, const int* split, 
    const double* b, double* x, int rows, int cols, int maxIters, double eps)
{
    for (int iter = 0; iter < maxIters; iter++)
    {
        int count = 0;
        for (int i = 0; i < rows; i++)
        {
            double val = 0;
            const IndexedValue* ptrRow = A + cols * i;
            for (int j = 0; j < split[i]; j++)
                val += ptrRow[j].value * x[ptrRow[j].index];
            for (int j = split[i] + 1; j < length[i]; j++)
                val += ptrRow[j].value * x[ptrRow[j].index];
            val = b[i] - val;
            val /= ptrRow[split[i]].value;
            if (fabs(val - x[i]) < eps)
                count++;
            x[i] = val;
        }
        if (count == rows)
        {
            printf("count = %d, end\n", iter + 1);
            break;
        }
        if ((iter + 1) % 100 == 0)
        {
            printf("iter = %d\n", iter + 1);
        }
    }
}

void makeIndex(const cv::Mat& mask, cv::Mat& index, int& numElems)
{
    CV_Assert(mask.data && mask.type() == CV_8UC1);
    
    int rows = mask.rows, cols = mask.cols;
    index.create(rows, cols, CV_32SC1);
    index.setTo(-1);
    int count = 0;
    for (int i = 0; i < rows; i++)
    {
        const unsigned char* ptrMaskRow = mask.ptr<unsigned char>(i);
        int* ptrIndexRow = index.ptr<int>(i);
        for (int j = 0; j < cols; j++)
        {
            if (ptrMaskRow[j])
                ptrIndexRow[j] = (count++);
        }
    }
    numElems = count;
}

void draw(const std::vector<cv::Point>& contour, cv::Size& imageSize, cv::Rect& extendRect, cv::Mat& mask)
{
    cv::Rect contourRect = cv::boundingRect(contour);
    int left, right, top, bottom;
    left = contourRect.x;
    right = contourRect.x + contourRect.width;
    top = contourRect.y;
    bottom = contourRect.y + contourRect.height;
    if (left > 0) left--;
    if (right < imageSize.width) right++;
    if (top > 0) top--;
    if (bottom < imageSize.height) bottom++;
    extendRect.x = left;
    extendRect.y = top;
    extendRect.width = right - left;
    extendRect.height = bottom - top;
    mask.create(extendRect.height, extendRect.width, CV_8UC1);
    mask.setTo(0);
    std::vector<std::vector<cv::Point> > contours(1);
    contours[0] = contour;
    cv::drawContours(mask, contours, -1, cv::Scalar(255), -1, 8, cv::noArray(), 0, cv::Point(-left, -top));
}

void getEquation(const cv::Mat& src, const cv::Mat& dst, 
    const cv::Mat& mask, const cv::Mat& index, int count,
    SparseMat& A, cv::Mat& b, cv::Mat& x)
{
    CV_Assert(src.data && dst.data && mask.data && index.data);
    CV_Assert((src.type() == CV_8UC1) && (dst.type() == CV_8UC1) &&
        (mask.type() == CV_8UC1) && (index.type() == CV_32SC1));
    CV_Assert((src.size() == dst.size()) && (src.size() == mask.size()) && (src.size() == index.size()));
    
    int rows = src.rows, cols = src.cols;
    A.create(count, 8);
    b.create(count, 1, CV_64FC1);
    b.setTo(0);
    x.create(count, 1, CV_64FC1);
    x.setTo(0);

    for (int i = 0; i < rows; i++)
    {
        for (int j = 0; j < cols; j++)
        {
            if (mask.at<unsigned char>(i, j))
            {
                int currIndex = index.at<int>(i, j);
                int currSrcVal = src.at<unsigned char>(i, j);
                int neighborCount = 0;
                int bVal = 0;
                if (i > 0)
                {
                    neighborCount++;
                    if (mask.at<unsigned char>(i - 1, j))
                    {
                        int topIndex = index.at<int>(i - 1, j);
                        A.insert(currIndex, topIndex, -1);
                    }
                    else
                    {
                        bVal += dst.at<unsigned char>(i - 1, j);
                    }
                    bVal += (currSrcVal - src.at<unsigned char>(i - 1, j));
                }
                if (i < rows - 1)
                {
                    neighborCount++;
                    if (mask.at<unsigned char>(i + 1, j))
                    {
                        int bottomIndex = index.at<int>(i + 1, j);
                        A.insert(currIndex, bottomIndex, -1);
                    }
                    else
                    {
                        bVal += dst.at<unsigned char>(i + 1, j);
                    }
                    bVal += (currSrcVal - src.at<unsigned char>(i + 1, j));
                }
                if (j > 0)
                {
                    neighborCount++;
                    if (mask.at<unsigned char>(i, j - 1))
                    {
                        int leftIndex = index.at<int>(i, j - 1);
                        A.insert(currIndex, leftIndex, -1);
                    }
                    else
                    {
                        bVal += dst.at<unsigned char>(i, j - 1);
                    }
                    bVal += (currSrcVal - src.at<unsigned char>(i, j - 1));
                }
                if (j < cols - 1)
                {
                    neighborCount++;
                    if (mask.at<unsigned char>(i, j + 1))
                    {
                        int rightIndex = index.at<int>(i, j + 1);
                        A.insert(currIndex, rightIndex, -1);
                    }
                    else
                    {
                        bVal += dst.at<unsigned char>(i, j + 1);
                    }
                    bVal += (currSrcVal - src.at<unsigned char>(i, j + 1));
                }
                A.insert(currIndex, currIndex, neighborCount);
                b.at<double>(currIndex) = bVal;
                //x.at<double>(currIndex) = currSrcVal;
                x.at<double>(currIndex) = dst.at<unsigned char>(i, j);
            }
        }
    }
}

void copy(const cv::Mat& val, const cv::Mat& mask, const cv::Mat& index, cv::Mat& dst)
{
    CV_Assert(val.data && val.type() == CV_64FC1);
    CV_Assert(mask.data && index.data && dst.data);
    CV_Assert((mask.type() == CV_8UC1) && (index.type() == CV_32SC1) && (dst.type() == CV_8UC1));
    CV_Assert((mask.size() == index.size()) && (mask.size() == dst.size()));

    int rows = mask.rows, cols = mask.cols;
    for (int i = 0; i < rows; i++)
    {
        const unsigned char* ptrMaskRow = mask.ptr<unsigned char>(i);
        const int* ptrIndexRow = index.ptr<int>(i);
        unsigned char* ptrDstRow = dst.ptr<unsigned char>(i);
        for (int j = 0; j < cols; j++)
        {
            if (ptrMaskRow[j])
            {
                ptrDstRow[j] = cv::saturate_cast<unsigned char>(val.at<double>(ptrIndexRow[j]));
            }
        }
    }
}

void main()
{
    //double A[] = {10, -1, 2, 0,
    //             -1, 11, -1, 3,
    //             2, -1, 10, -1,
    //             0, 3, -1, 8};
    //double b[] = {6, 25, -11, 15};
    //double x[] = {0, 0, 0, 0};
    //double r[] = {0, 0, 0, 0};
    //solve(A, b, r, 4, 4 * sizeof(double), 1000, 0.0001);
    //return;

    //std::vector<std::vector<cv::Point> > contours(1);
    //contours[0].resize(4);
    //contours[0][0] = cv::Point(10, 10);
    //contours[0][1] = cv::Point(10, 30);
    //contours[0][2] = cv::Point(30, 30);
    //contours[0][3] = cv::Point(30, 10);
    //cv::Mat image = cv::Mat::zeros(100, 100, CV_8UC1);
    //cv::drawContours(image, contours, -1, cv::Scalar(255), -1, 8, cv::noArray(), 0, cv::Point(-10, -10));
    //cv::imshow("image", image);
    //cv::waitKey(0);

    SparseMat sMat(2, 8);
    sMat.insert(0, 5, 0.2);
    sMat.insert(0, 6, 0.5);
    sMat.insert(0, 3, 1.0);
    sMat.insert(0, 1, 3.0);
    sMat.insert(0, 8, 2.0);
    sMat.insert(0, 2, 8.0);
    sMat.insert(1, 5, 7.0);
    sMat.insert(1, 4, 0.5);
    //return;

    cv::Mat src/*Color*/ = cv::imread("C:\\Users\\zhengxuping\\Desktop\\QQ截图20150608184426.bmp");
    cv::Mat dst/*Color*/ = cv::imread("C:\\Users\\zhengxuping\\Desktop\\QQ截图20150609111926.bmp");

    //cv::Mat src, dst;
    //cv::cvtColor(srcColor, src, CV_BGR2GRAY);
    //cv::cvtColor(dstColor, dst, CV_BGR2GRAY);

    std::vector<cv::Point> contour(4);
    contour[0] = cv::Point(40, 40);
    contour[1] = cv::Point(40, 150);
    contour[2] = cv::Point(100, 150);
    contour[3] = cv::Point(100, 40);
    cv::Rect extendRect;
    cv::Mat mask, index;
    SparseMat A;
    cv::Mat b, x;
    int numElems;

    draw(contour, src.size(), extendRect, mask);
    cv::imshow("mask", mask);
    cv::waitKey(0);
    makeIndex(mask, index, numElems);

    cv::Mat srcROI(src, extendRect), dstROI(dst, extendRect);
    
    //cv::imshow("src roi", srcROI);
    //cv::imshow("dst roi", dstROI);
    //cv::waitKey(0);

    if (src.type() == CV_8UC1)
    {
        getEquation(srcROI, dstROI, mask, index, numElems, A, b, x);
        std::vector<int> split;
        A.calcSplit(split);
        solve(A.data, &A.count[0], &split[0], (double*)b.data, (double*)x.data, A.rows, A.maxCols, 10000, 0.01);
        copy(x, mask, index, dstROI);
    }
    else if (src.type() == CV_8UC3)
    {
        cv::Mat srcROISplit[3], dstROISplit[3];
        for (int i = 0; i < 3; i++)
        {
            srcROISplit[i].create(srcROI.size(), CV_8UC1);
            dstROISplit[i].create(dstROI.size(), CV_8UC1);
        }
        cv::split(srcROI, srcROISplit);
        cv::split(dstROI, dstROISplit);
        //cv::imshow("src split 0", srcROISplit[0]);
        //cv::imshow("src split 1", srcROISplit[1]);
        //cv::imshow("src split 2", srcROISplit[2]);
        //cv::imshow("dst split 0", dstROISplit[0]);
        //cv::imshow("dst split 1", dstROISplit[1]);
        //cv::imshow("dst split 2", dstROISplit[2]);
        //cv::waitKey(0);

        for (int i = 0; i < 3; i++)
        {
            getEquation(srcROISplit[i], dstROISplit[i], mask, index, numElems, A, b, x);
            std::vector<int> split;
            A.calcSplit(split);
            solve(A.data, &A.count[0], &split[0], (double*)b.data, (double*)x.data, A.rows, A.maxCols, 10000, 0.01);
            copy(x, mask, index, dstROISplit[i]);
        }
        cv::merge(dstROISplit, 3, dstROI);
    }

    cv::imshow("src", src);
    cv::imshow("dst", dst);
    cv::waitKey(0);
    //cv::imwrite("dst.bmp", dst);
}