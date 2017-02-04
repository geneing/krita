/*
 *  Copyright (c) 2016 Eugene Ingerman
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

/**
 * Inpaint using the PatchMatch Algorithm
 *
 * | PatchMatch : A Randomized Correspondence Algorithm for Structural Image Editing
 * | by Connelly Barnes and Eli Shechtman and Adam Finkelstein and Dan B Goldman
 * | ACM Transactions on Graphics (Proc. SIGGRAPH), vol.28, aug-2009
 *
 * Original author Xavier Philippeau
 * Code adopted from: David Chatting https://github.com/davidchatting/PatchMatch
 */

#include <boost/multi_array.hpp>
#include <random>

#include "kis_paint_device.h"

#include "kis_clone_test.h"
#include "kis_debug.h"
#include "kis_paint_device_debug_utils.h"
#include "kis_random_accessor_ng.h"

#include <QTest>

#include <QList>
#include <kis_transform_worker.h>
#include <kis_filter_strategy.h>
#include "KoColor.h"
#include "KoColorSpace.h"
#include "KoChannelInfo.h"
#include "KoMixColorsOp.h"

#include <KisPart.h>
#include <kis_group_layer.h>
#include <qimage_based_test.h>
#include <stroke_testing_utils.h>
#include <brushengine/kis_paint_information.h>
#include <kis_canvas_resource_provider.h>
#include <brushengine/kis_paintop_preset.h>
#include <brushengine/kis_paintop_settings.h>

#if 1
#include "hdf5.h"
#include "hdf5_hl.h"
#endif

#define isOdd(x) ((x) & 0x01)

const int MAX_DIST = 65535;
const quint8 MASK_SET = 0;
const quint8 MASK_CLEAR = 255;

class ImageView {
protected:
    quint8* m_data;
    int m_imageWidth;
    int m_imageHeight;
    int m_pixelSize;

public:
    void Init( quint8* _data, int _imageWidth, int _imageHeight, int _pixelSize ){
        m_data = _data;
        m_imageWidth = _imageWidth;
        m_imageHeight = _imageHeight;
        m_pixelSize = _pixelSize;
    }

    ImageView() : m_data(nullptr) {
        m_imageHeight =  m_imageWidth = m_pixelSize = 0;
    }

    ImageView(quint8* _data, int _imageWidth, int _imageHeight, int _pixelSize){
        Init( _data, _imageWidth, _imageHeight, _pixelSize );
    }

    quint8* operator ()(int x, int y) const{
        Q_ASSERT( m_data );
        Q_ASSERT( (x>=0) && (x<m_imageWidth) && (y>=0) && (y<m_imageHeight) );
        return (m_data+x*m_pixelSize+y*m_imageWidth*m_pixelSize);
    }

    ImageView& operator=( const ImageView& other ) {
        if (this != &other) {
            if(other.num_bytes() != num_bytes() ){
                delete[] m_data;
                m_data = nullptr; //to preserve invariance if next line throws exception
                m_data = new quint8[other.num_bytes()];
            }
            std::copy(other.data(), other.data()+other.num_bytes(), m_data);
            m_imageHeight = other.m_imageHeight;
            m_imageWidth = other.m_imageWidth;
            m_pixelSize = other.m_pixelSize;
        }
        return *this;
    }

    //move assignement operator
    ImageView& operator=(ImageView&& other) noexcept {
        if( this != &other ){
            delete[] m_data;
            m_data = nullptr;
            Init(other.data(), other.m_imageWidth, other.m_imageHeight, other.m_pixelSize);
            other.m_data = nullptr;
        }
        return *this;
    }

    quint8* data( void ) const { return m_data; }
    inline int num_elements( void ) const { return m_imageHeight*m_imageWidth; }
    inline int num_bytes( void ) const { return m_imageHeight*m_imageWidth*m_pixelSize; }
    inline int pixel_size(void) const { return m_pixelSize; }
};

class ImageData : public ImageView{
    QRect m_imageSize;

public:
    ImageData() : ImageView () {}

    void Init( int _imageWidth, int _imageHeight, int _pixelSize ){
        m_imageSize.setSize(QSize(_imageWidth, _imageHeight));
        m_data = new quint8[ _imageWidth*_imageHeight*_pixelSize ];
        ImageView::Init( m_data, _imageWidth, _imageHeight, _pixelSize );
    }

    ImageData( int _imageWidth, int _imageHeight, int _pixelSize ) : ImageView() {
        Init( _imageWidth, _imageHeight, _pixelSize );
    }

    void Init( KisPaintDeviceSP imageDev, const QRect& imageSize ){
        m_imageSize = imageSize;

        const KoColorSpace* cs = imageDev->colorSpace();
        m_pixelSize = cs->pixelSize();

        m_data = new quint8[ imageSize.width()*imageSize.height()*cs->pixelSize() ];
        imageDev->readBytes( m_data, imageSize.x(), imageSize.y(), imageSize.width(), imageSize.height() );
        ImageView::Init( m_data, imageSize.width(), imageSize.height(), m_pixelSize );
    }

    ImageData( KisPaintDeviceSP imageDev, const QRect& imageSize) : ImageView(){
        Init( imageDev, imageSize );
    }

    ~ImageData(){
        delete[] m_data;
    }

    void saveToDevice( KisPaintDeviceSP outDev ){
        Q_ASSERT(outDev->colorSpace()->pixelSize() == m_pixelSize);
        outDev->writeBytes( m_data, m_imageSize );
    }
};

inline void alignRectBy2(qint32 &x, qint32 &y, qint32 &w, qint32 &h)
{
    x -= isOdd(x);
    y -= isOdd(y);
    w += isOdd(x);
    w += isOdd(w);
    h += isOdd(y);
    h += isOdd(h);
}


class MaskedImage : public KisShared
{
private:

    QRect imageSize;
    int nChannels;

    const KoColorSpace* cs;

    ImageData maskData;
    ImageData imageData;


    void cacheImageSize(KisPaintDeviceSP imageDev)
    {
        imageSize = imageDev->exactBounds();
    }

    void cacheImage(KisPaintDeviceSP imageDev)
    {
        Q_ASSERT(!imageSize.isEmpty() && imageSize.isValid());
        cs = imageDev->colorSpace();
        nChannels = cs->channelCount();
        imageData.Init(imageDev, imageSize);
    }


    void cacheMask(KisPaintDeviceSP maskDev)
    {
        Q_ASSERT(!imageSize.isEmpty() && imageSize.isValid());
        Q_ASSERT( maskDev->colorSpace()->pixelSize() == 1);
        maskData.Init( maskDev, imageSize );
    }

    MaskedImage() {}

public:

    void toPaintDevice( KisPaintDeviceSP imageDev )
    {
        imageData.saveToDevice( imageDev );
    }

    void clearMask(void)
    {
        std::fill(maskData.data(), maskData.data()+maskData.num_bytes(), MASK_CLEAR);
    }

    void initialize(KisPaintDeviceSP _imageDev, KisPaintDeviceSP _maskDev)
    {
        cacheImageSize(_imageDev);
        cacheImage(_imageDev);
        cacheMask(_maskDev);
    }

//    void clone(const MaskeImageSP other)
//    {
//        return new MaskedImage(imageDev, maskDev);
//    }

    MaskedImage(KisPaintDeviceSP _imageDev, KisPaintDeviceSP _maskDev)
    {
        initialize(_imageDev, _maskDev);
    }

    void downsample2x(void)
    {
        int kernel[6] = {1,5,10,10,5,1};

        int H=imageSize.height();
        int W=imageSize.width();
        int newW=W/2, newH=H/2;

        ImageData newImage(newW, newH, cs->pixelSize());
        std::fill(newImage.data(), newImage.data()+newImage.num_bytes(), 0);

        ImageData newMask(newW, newH, 1);

        QVector<float> colors(nChannels, 0.f);

        for (int x=0;x<W-1;x+=2) {
            for (int y=0;y<H-1;y+=2) {
                int ksum=0;
                int m=0;

                for (int dy=-2;dy<=3;++dy) {
                    int yk=y+dy;
                    if (yk<0 || yk>=H)
                        continue;

                    int ky = kernel[2+dy];
                    for (int dx=-2;dx<=3;++dx) {
                        int xk = x+dx;
                        if (xk<0 || xk>=W)
                            continue;

                        if (isMasked(xk, yk))
                            continue;

                        int k = kernel[2+dx]*ky;
                        QVector<float> v = getImagePixels(xk, yk);
                        for(int i=0; i<nChannels; ++i){
                            colors[i] += k*v[i];
                        }
                        ksum+=k;
                        m++;
                    }
                }
                if (ksum>0) {
                    for(int i=0; i<nChannels; ++i){
                        colors[i] /= ksum;
                    }
                }

                if (m!=0) {
                    cs->fromNormalisedChannelsValue(newImage(x/2,y/2), colors);
                    *newMask(x/2,y/2) = MASK_CLEAR;
                } else {
                    *newMask(x/2,y/2) = MASK_SET;
                }
            }
        }

        imageData = std::move(newImage);
        maskData = std::move(newMask);

        imageSize = QRect(0, 0, newW, newH);
        int nmasked = countMasked();
        printf("Masked: %d size: %dx%d\n", nmasked, newW, newH);
    }

    //TODO replace this with reference code if not working
    void upscale(int newW, int newH)
    {
        int H=imageSize.height();
        int W=imageSize.width();

        ImageData newImage(newW, newH, cs->pixelSize());
        ImageData newMask(newW, newH, 1);

        QVector<float> colors(nChannels, 0.f);
        QVector<float> v(nChannels, 0.f);

        for (int x=0; x<newW; ++x) {
            for (int y=0; y<newH; ++y) {

                // original pixel
                int xs = (x*W)/newW;
                int ys = (y*H)/newH;

                // copy to new image
                if (!isMasked(xs, ys)) {
                    std::copy(imageData(xs,ys), imageData(xs,ys)+imageData.pixel_size(), newImage(x,y));
                    *newMask(x,y) = MASK_CLEAR;
                } else {
                    std::fill(newImage(x,y), newImage(x,y)+newImage.pixel_size(), 0);
                    *newMask(x,y) = MASK_SET;
                }
            }
        }

        imageData = std::move(newImage);
        maskData = std::move(newMask);
        imageSize = QRect(0, 0, newW, newH);
    }

    QRect size()
    {
        return imageSize;
    }

    KisSharedPtr<MaskedImage> copy( void )
    {
        KisSharedPtr<MaskedImage> clone = new MaskedImage();
        clone->imageSize = this->imageSize;
        clone->nChannels = this->nChannels;
        clone->maskData = this->maskData;
        clone->imageData = this->imageData;
        clone->cs = this->cs;

        return clone;
    }

    int countMasked(void)
    {
        int count = std::count_if(maskData.data(), maskData.data() + maskData.num_elements(), [](quint8 v){return v<((MASK_SET+MASK_CLEAR/2));});
        return count;
    }

    inline bool isMasked(int x, int y)
    {
        return (*maskData(x,y)<((MASK_SET+MASK_CLEAR)/2));
    }

    //returns true if the patch contains a masked pixel
    bool containsMasked(int x, int y, int S)
    {
        for (int dy = -S; dy <= S; ++dy) {
            int ys = y + dy;
            for (int dx = -S; dx <= S; ++dx) {
                int xs = x + dx;
                if (xs < 0 || xs >= imageSize.width())
                    continue;
                if (ys < 0 || ys >= imageSize.height())
                    continue;
                if (isMasked(xs, ys))
                    return true;
            }
        }
        return false;
    }

//    const quint8* getImagePixel(int x, int y) {
//        KisRandomConstAccessorSP it = imageDev->createRandomConstAccessorNG(x, y);
//        return it->oldRawData(); //is this Ok to do?
//    }

    inline quint8 getImagePixelU8(int x, int y, int chan) const
    {
        return cs->scaleToU8(imageData(x,y), chan);
    }

    inline QVector<float> getImagePixels(int x, int y) const
    {
        QVector<float> v(cs->channelCount());
        cs->normalisedChannelsValue(imageData(x,y), v);
        return v;
    }

    inline void setImagePixels(int x, int y, QVector<float>& value)
    {
        cs->fromNormalisedChannelsValue(imageData(x,y), value);
    }

    inline void setMask(int x, int y, quint8 v)
    {
        *(maskData(x,y)) = v;
    }

    inline int channelCount(void) const{
        return cs->channelCount();
    }

    float distance(int x, int y, const MaskedImage& other, int xo, int yo)
    {
        float dsq = 0;
        quint32 nchannels = channelCount();
        for (quint32 chan = 0; chan < nchannels; chan++) {
            quint8 v = getImagePixelU8(x,y, chan) - other.getImagePixelU8(xo,yo,chan);
            dsq += v * v;
        }
        return dsq;
    }




//    void debugDump(QString dirName, QString dset)
//    {
//        static int index = 0;
//        hid_t       file_id;

//        QString fname = dirName + QString("debug.h5"); //suffix+QString::number(index);
//        file_id = H5Fopen(fname.toStdString().c_str(), H5F_ACC_RDWR, H5P_DEFAULT);
//        if (file_id == -1)
//            file_id = H5Fcreate(fname.toStdString().c_str(), H5F_ACC_DEBUG, H5P_DEFAULT, H5P_DEFAULT);

//        QString dset_name = QString("/") + dset + QString("_mask_") + QString::number(index);
//        H5LTmake_dataset(file_id, dset_name.toStdString().c_str(), maskView.num_dimensions(), (const hsize_t*)maskView.shape(), H5T_NATIVE_B8, (void*)maskView.data());

//        dset_name = QString("/") + dset + QString("_image_") + QString::number(index);
//        H5LTmake_dataset(file_id, dset_name.toStdString().c_str(), imageView.num_dimensions(), (const hsize_t*)imageView.shape(), H5T_NATIVE_FLOAT, (void*)imageView.data());

//        H5Fclose(file_id);
//        index++;
//    }

};

typedef KisSharedPtr<MaskedImage> MaskedImageSP;

struct NNPixel {
    int x;
    int y;
    int distance;
};
typedef boost::multi_array<NNPixel, 2> NNArray_type;

struct Vote_elem {
    QVector<float> channel_values;
    float w;
};
typedef boost::multi_array<Vote_elem, 2> Vote_type;



class NearestNeighborField : public KisShared
{

private:
    template< typename T> T random_int(T range)
    {
//        static std::random_device rd;
//        static std::mt19937 gen(rd());

//        std::uniform_int_distribution<T> dis(0, range - 1);
//        return dis(gen);
        return rand() % range;
    }

    //compute intial value of the distance term
    void initialize(void)
    {
        for (int y = 0; y < imSize.height(); y++) {
            for (int x = 0; x < imSize.width(); x++) {
                field[x][y].distance = distance(x, y, field[x][y].x, field[x][y].y);

                //if the distance is "infinity", try to find a better link
                int iter = 0;
                const int maxretry = 20;
                while (field[x][y].distance == MAX_DIST && iter < maxretry) {
                    field[x][y].x = random_int(imSize.width()+1);
                    field[x][y].y = random_int(imSize.height()+1);
                    field[x][y].distance = distance(x, y, field[x][y].x, field[x][y].y);
                    iter++;
                }
            }
        }
    }

//    void init_similarity_curve(void)
//    {
//        float s_zero = 0.999;
//        float t_halfmax = 0.10;

//        float x  = (s_zero - 0.5) * 2;
//        float invtanh = 0.5 * std::log((1. + x) / (1. - x));
//        float coef = invtanh / t_halfmax;

//        similarity.resize(MAX_DIST + 1);
//        for (int i = 0; i < similarity.size(); i++) {
//            float t = (float)i / similarity.size();
//            similarity[i] = 0.5 - 0.5 * std::tanh(coef * (t - t_halfmax));
//        }
//    }

    void init_similarity_curve(void)
    {
        float base[] = {1.0, 0.99, 0.96, 0.83, 0.38, 0.11, 0.02, 0.005, 0.0006, 0.0001, 0 };

        similarity.resize(MAX_DIST + 1);
        for (int i = 0; i < similarity.size(); i++) {
            float t = (float)i / similarity.size();
            int j = (int)(100*t);
            int k = j+1;
            float vj = (j<11)? base[j]:0;
            float vk = (k<11)? base[k]:0;
            similarity[i] = vj + (100*t-j)*(vk-vj);
        }
    }

private:
    int patchSize; //patch size
public:
    MaskedImageSP input;
    MaskedImageSP output;
    QRect imSize;
    NNArray_type field;
    std::vector<float> similarity;
    quint32 nColors;
    QList<KoChannelInfo *> channels;

    typedef boost::multi_array<float, 2> HistArray_type;
    HistArray_type histogram;

public:
    NearestNeighborField(const MaskedImageSP _input, MaskedImageSP _output, int _patchsize) : input(_input), output(_output), patchSize(_patchsize)
    {
        imSize = input->size();
        field.resize(boost::extents[imSize.width()][imSize.height()]);
        init_similarity_curve();

        nColors = input->channelCount(); //only color count, doesn't include alpha channels
    }

    void randomize(void)
    {
        for (int y = 0; y < imSize.height(); y++) {
            for (int x = 0; x < imSize.width(); x++) {
                field[x][y].x = random_int(imSize.width()+1);
                field[x][y].y = random_int(imSize.height()+1);
                field[x][y].distance = MAX_DIST;
            }
        }
        initialize();
    }

    //initialize field from an existing (possibly smaller) nearest neighbor field
    void initialize(const NearestNeighborField& nnf)
    {
        float xscale = imSize.width() / nnf.imSize.width();
        float yscale = imSize.height() / nnf.imSize.height();

        for (int y = 0; y < imSize.height(); y++) {
            for (int x = 0; x < imSize.width(); x++) {
                int xlow = std::min((int)(x / xscale), nnf.imSize.width() - 1);
                int ylow = std::min((int)(y / yscale), nnf.imSize.height() - 1);

                field[x][y].x = nnf.field[xlow][ylow].x * xscale;
                field[x][y].y = nnf.field[xlow][ylow].y * yscale;
                field[x][y].distance = MAX_DIST;
            }
        }
        initialize();
    }

    //multi-pass NN-field minimization (see "PatchMatch" - page 4)
    void minimize(int pass)
    {
        int min_x = 0;
        int min_y = 0;
        int max_x = imSize.width() - 1;
        int max_y = imSize.height() - 1;

        for (int i = 0; i < pass; i++) {
            //scanline order
            for (int y = min_y; y < max_y; y++)
                for (int x = min_x; x <= max_x; x++)
                    if (field[x][y].distance > 0)
                        minimizeLink(x, y, 1);

            //reverse scanline order
            for (int y = max_y; y >= min_y; y--)
                for (int x = max_x; x >= min_x; x--)
                    if (field[x][y].distance > 0)
                        minimizeLink(x, y, -1);
        }
    }

    void minimizeLink(int x, int y, int dir)
    {
        int xp, yp, dp;

        //Propagation Left/Right
        if (x - dir > 0 && x - dir < imSize.width()) {
            xp = field[x - dir][y].x + dir;
            yp = field[x - dir][y].y;
            dp = distance(x, y, xp, yp);
            if (dp < field[x][y].distance) {
                field[x][y].x = xp;
                field[x][y].y = yp;
                field[x][y].distance = dp;
            }
        }

        //Propagation Up/Down
        if (y - dir > 0 && y - dir < imSize.height()) {
            xp = field[x][y - dir].x;
            yp = field[x][y - dir].y + dir;
            dp = distance(x, y, xp, yp);
            if (dp < field[x][y].distance) {
                field[x][y].x = xp;
                field[x][y].y = yp;
                field[x][y].distance = dp;
            }
        }

        //Random search
        int wi = std::max(output->size().width(),output->size().height());
        int xpi = field[x][y].x;
        int ypi = field[x][y].y;
        while (wi > 0) {
            xp = xpi + random_int(2 * wi) - wi;
            yp = ypi + random_int(2 * wi) - wi;
            xp = std::max(0, std::min(output->size().width() - 1, xp));
            yp = std::max(0, std::min(output->size().height() - 1, yp));

            dp = distance(x, y, xp, yp);
            if (dp < field[x][y].distance) {
                field[x][y].x = xp;
                field[x][y].y = yp;
                field[x][y].distance = dp;
            }
            wi /= 2;
        }
    }

    //compute distance between two patches
    int distance(int x, int y, int xp, int yp)
    {
        float distance = 0;
        float wsum = 0;
        float ssdmax = 9 * 255 * 255;

        //for each pixel in the source patch
        for (int dy = -patchSize; dy <= patchSize; dy++) {
            for (int dx = -patchSize; dx <= patchSize; dx++) {
                wsum++;
                int xks = x + dx;
                int yks = y + dy;

                if (xks < 1 || xks >= input->size().width()-1) {
                    distance++;
                    continue;
                }

                if (yks < 1 || yks >= input->size().height()-1) {
                    distance++;
                    continue;
                }

                //cannot use masked pixels as a valid source of information
                if (input->isMasked(xks, yks)) {
                    distance++;
                    continue;
                }

                //corresponding pixel in target patch
                int xkt = xp + dx;
                int ykt = yp + dy;
                if (xkt < 1 || xkt >= output->size().width()-1) {
                    distance++;
                    continue;
                }
                if (ykt < 1 || ykt >= output->size().height()-1) {
                    distance++;
                    continue;
                }

                //cannot use masked pixels as a valid source of information
                if (output->isMasked(xkt, ykt)) {
                    distance++;
                    continue;
                }

                //SSD distance between pixels
                float ssd = input->distance(xks, yks, *output, xkt, ykt);
                //long ssd = input->distance(xks, yks, *input, xkt, ykt);
                distance += ssd/ssdmax;

            }
        }
        return (int)((MAX_DIST * distance) / wsum);
    }

    static MaskedImageSP ExpectationMaximization(KisSharedPtr<NearestNeighborField> SourceToTarget, KisSharedPtr<NearestNeighborField> TargetToSource, int level, int radius, QList<MaskedImageSP>& pyramid);

    static void ExpectationStep(KisSharedPtr<NearestNeighborField> nnf, bool sourceToTarget, Vote_type& vote, MaskedImageSP source, bool upscale);

    void EM_Step(MaskedImageSP source, MaskedImageSP target, int R, bool upscaled);

    void debugDumpHistogram(QString dirName, QString dset)
    {
        static int index = 0;
        hid_t       file_id;

        QString fname = dirName + QString("debug.h5"); //suffix+QString::number(index);
        file_id = H5Fopen(fname.toStdString().c_str(), H5F_ACC_RDWR, H5P_DEFAULT);
        if (file_id == -1)
            file_id = H5Fcreate(fname.toStdString().c_str(), H5F_ACC_DEBUG, H5P_DEFAULT, H5P_DEFAULT);

        QString dset_name = QString("/") + dset + QString("_histogram_") + QString::number(index);
        H5LTmake_dataset(file_id, dset_name.toStdString().c_str(), histogram.num_dimensions(), (const hsize_t*)histogram.shape(), H5T_NATIVE_FLOAT, (void*)histogram.data());

        H5Fclose(file_id);

        index++;
    }

    void debugDumpField(QString dirName, QString dset)
    {
        static int index = 0;
        hid_t       file_id;

        QString fname = dirName + QString("debug.h5"); //suffix+QString::number(index);
        file_id = H5Fopen(fname.toStdString().c_str(), H5F_ACC_RDWR, H5P_DEFAULT);
        if (file_id == -1)
            file_id = H5Fcreate(fname.toStdString().c_str(), H5F_ACC_DEBUG, H5P_DEFAULT, H5P_DEFAULT);

        char dsetName[1024];
        snprintf(dsetName, 1023, "/field_%s_%d", dset.toStdString().c_str(), index);

        hsize_t dims[] = {field.shape()[0], field.shape()[1], 3}; //x,y,distance
        H5LTmake_dataset(file_id, dsetName, 3, dims, H5T_NATIVE_INT, (void*)field.data());

        H5Fclose(file_id);

        index++;
    }

};
typedef KisSharedPtr<NearestNeighborField> NearestNeighborFieldSP;


class Inpaint
{
private:
    KisPaintDeviceSP devCache;
    MaskedImageSP initial;
    NearestNeighborFieldSP nnf_TargetToSource;
    NearestNeighborFieldSP nnf_SourceToTarget;
    int radius;
    QList<MaskedImageSP> pyramid;


public:
    Inpaint(KisPaintDeviceSP dev, KisPaintDeviceSP devMask, int _radius)
    {
        initial = new MaskedImage(dev, devMask);
        radius = _radius;
        devCache = dev;
    }
    MaskedImageSP patch(void);
    MaskedImageSP patch_simple(void);
};

class TestClone : public TestUtil::QImageBasedTest
{
public:
    TestClone() : QImageBasedTest("clonetest") {}
    virtual ~TestClone() {}
    void test();
    void testPatchMatch();
private:
    MaskedImageSP patchImage(KisPaintDeviceSP, KisPaintDeviceSP, int radius);
};


//MaskedImageSP Inpaint::patch_simple()
//{
//    MaskedImageSP source = initial->initialize();
//    MaskedImageSP target = initial->initialize();

////    int nMasked=source->countMasked();
////    int level = 0;
//    QRect szOrig = source->size();
////    //printf("Level: %d, Masked: %d\n", level, nMasked);
////    printf("Level: %d, Masked: %d, Size: %d x %d\n", level, nMasked, szOrig.width(), szOrig.height());

////    do{
////        source->downsample2x();
////        source = source->upscale(szOrig.width(), szOrig.height());
////        QRect size = source->size();
////        level++;
////        nMasked = source->countMasked();
////        printf("Level: %d, Masked: %d, Size: %d x %d\n", level, nMasked, size.width(), size.height());
////        KIS_DUMP_DEVICE_2(source->getImageDev(),source->size(),"source","/home/eugening/Projects/Src");
////        KIS_DUMP_DEVICE_2(source->getMaskDev(),source->size(),"mask","/home/eugening/Projects/Src");
////    }while(nMasked>0 && level < 5);

//    source->downsample2x();
//    NearestNeighborFieldSP nnf = new NearestNeighborField(source, source, radius);
//    nnf->randomize();
//    nnf->minimize(4);
//    nnf->debugDumpField("/home/eugening/Projects/", "NNF");

//    source->upscale(szOrig.width(), szOrig.height());

//    NearestNeighborFieldSP nnf_TargetToSource = new NearestNeighborField(target, source, radius);
//    nnf_TargetToSource->initialize(*nnf);
//    nnf_TargetToSource->debugDumpField("/home/eugening/Projects/", "NNF");
//    nnf_TargetToSource->minimize(4);
//    nnf_TargetToSource->debugDumpField("/home/eugening/Projects/", "NNF");
//    nnf_TargetToSource->EM_Step(source, target, 4, true);
//    nnf_TargetToSource->debugDumpField("/home/eugening/Projects/", "NNF");
//    //KIS_DUMP_DEVICE_2(source->getImageDev(), source->size(), "source", "/home/eugening/Projects/Src");
//    //KIS_DUMP_DEVICE_2(target->getImageDev(), target->size(), "target", "/home/eugening/Projects/Tgt");

////    nnf_TargetToSource = new NearestNeighborField( source, target, radius);
////    nnf_TargetToSource->randomize();
////    nnf_TargetToSource->debugDumpField("/home/eugening/Projects/","NNF");

////    //minimize the NNF
////    nnf_TargetToSource->minimize(4);
////    nnf_TargetToSource->debugDumpField("/home/eugening/Projects/","NNF");
////    nnf_TargetToSource->EM_Step(source, target, 4, false);
////    KIS_DUMP_DEVICE_2(target->getImageDev(),target->size(),"target","/home/eugening/Projects/Tgt");

//    return target;
//}


MaskedImageSP Inpaint::patch()
{
    MaskedImageSP source = initial->copy();

    pyramid.append(initial);

    QRect size = source->size();

    while ((size.width() > radius) && (size.height() > radius) && source->countMasked() > 0 ) {

        source->downsample2x();
        pyramid.append(source->copy());
        size = source->size();
    }
    int maxlevel = pyramid.size();

    // The initial target is the same as the smallest source.
    // We consider that this target contains no masked pixels
    MaskedImageSP target = nullptr;
//    target->clearMask();

    //recursively building nearest neighbor field
    for (int level = maxlevel - 1; level > 0; level--) {
        source = pyramid.at(level);
        if (level == maxlevel - 1) {
            target = source->copy();
            target->clearMask();

            nnf_SourceToTarget = new NearestNeighborField(source, target, radius);
            nnf_SourceToTarget->randomize();

            //random initial guess
            nnf_TargetToSource = new NearestNeighborField(target, source, radius);
            nnf_TargetToSource->randomize();
        } else {
            // then, we use the rebuilt (upscaled) target
            // and reuse the previous NNF as initial guess
            NearestNeighborFieldSP new_nnf = new NearestNeighborField(source, target, radius);
            new_nnf->initialize(*nnf_SourceToTarget);
            nnf_SourceToTarget = new_nnf;

            NearestNeighborFieldSP new_nnf_rev = new NearestNeighborField(target, source, radius);
            new_nnf_rev->initialize(*nnf_TargetToSource);
            nnf_TargetToSource = new_nnf_rev;
        }

        //Build an upscaled target by EM-like algorithm (see "PatchMatch" - page 6)
        target = NearestNeighborField::ExpectationMaximization(nnf_SourceToTarget, nnf_TargetToSource, level, radius, pyramid);

        //devCache->clear();
        target->toPaintDevice(devCache);
        KIS_DUMP_DEVICE_2(devCache, target->size(), "target", "/home/eugening/Projects/Tgt");
    }
    return target;
}

// Maximization Step : Maximum likelihood of target pixel
void MaximizationStep(MaskedImageSP target, const Vote_type& vote)
{
    QRect sz = target->size();

    int H = sz.height();
    int W = sz.width();

    for(int x=0 ; x < W; ++x){
        for(int y=0 ; y < H; ++y){
            if (vote[x][y].w>0) {
                QVector<float> pixel = vote[x][y].channel_values;
                float w = vote[x][y].w;
                for( auto&& v : pixel )
                    v /= w;
                target->setImagePixels(x, y, pixel);
                target->setMask(x, y, MASK_CLEAR);
            }
        }
    }
}

//EM-Like algorithm (see "PatchMatch" - page 6)
//Returns a float sized target image
MaskedImageSP NearestNeighborField::ExpectationMaximization(NearestNeighborFieldSP nnf_SourceToTarget, NearestNeighborFieldSP nnf_TargetToSource, int level, int radius, QList<MaskedImageSP>& pyramid)
{
    int iterEM = 1 + 2 * level;
    int iterNNF = std::min(7, 1 + level);

    MaskedImageSP source = nnf_SourceToTarget->input;
    MaskedImageSP target = nnf_SourceToTarget->output;
    MaskedImageSP newtarget = nullptr;

    //EM loop
    for (int emloop = 1; emloop <= iterEM; emloop++) {
        //set the new target as current target
        if (!newtarget.isNull()) {
            nnf_SourceToTarget->output = newtarget;
            nnf_TargetToSource->input = newtarget;
            target = newtarget;
            newtarget = nullptr;
        }

        // add constraints to the NNF
        for (int x = 0; x < source->size().width(); ++x) {
            for (int y = 0; y < source->size().height(); ++y) {
                if (!source->containsMasked(x, y, radius)){
                    nnf_SourceToTarget->field[x][y].x = x;
                    nnf_SourceToTarget->field[x][y].y = y;
                    nnf_SourceToTarget->field[x][y].distance = 0;
                }
            }
        }

        for (int x = 0; x < target->size().width(); ++x) {
            for (int y = 0; y < target->size().height(); ++y) {
                if (!source->containsMasked(x, y, radius)){
                    nnf_TargetToSource->field[x][y].x = x;
                    nnf_TargetToSource->field[x][y].y = y;
                    nnf_TargetToSource->field[x][y].distance = 0;
                }
            }
        }

        //minimize the NNF
        nnf_SourceToTarget->minimize(iterNNF);
        nnf_TargetToSource->minimize(iterNNF);

        nnf_SourceToTarget->debugDumpField("/home/eugening/Projects/","S2T");
        nnf_TargetToSource->debugDumpField("/home/eugening/Projects/","T2S");

        //Now we rebuild the target using best patches from source
        MaskedImageSP newsource = nullptr;
        bool upscaled = false;

        // Instead of upsizing the final target, we build the last target from the next level source image
        // So the final target is less blurry (see "Space-Time Video Completion" - page 5)
        if (level >= 1 && (emloop == iterEM)) {
            newsource = pyramid.at(level - 1);
            QRect sz = newsource->size();
            newtarget = target->copy();
            newtarget->upscale(sz.width(), sz.height());
            upscaled = true;
        } else {
            newsource = pyramid.at(level);
            newtarget = target->copy();
            upscaled = false;
        }
        //EM Step

        //EM_Step(newsource, newtarget, radius, upscaled);
        QRect sz = newtarget->size();
        Vote_type vote(boost::extents[sz.width()][sz.height()]);
        quint32 nChannels = nnf_SourceToTarget->input->channelCount();
        for( auto it = vote.origin(); it!= (vote.origin()+vote.num_elements()); ++it){
            it->channel_values = QVector<float>(nChannels, 0.);
            it->w = 0.f;
        }

        ExpectationStep(nnf_SourceToTarget, true, vote, newsource, upscaled);
        ExpectationStep(nnf_TargetToSource, false, vote, newsource, upscaled);

        // --- MAXIMIZATION STEP ---

        // compile votes and update pixel values
        MaximizationStep(newtarget, vote);

    }
    //newtarget->debugDump("/home/eugening/Projects/","newtarget");
    nnf_SourceToTarget->debugDumpField("/home/eugening/Projects/","S2T_Final");
    nnf_TargetToSource->debugDumpField("/home/eugening/Projects/","T2S_Final");
    return newtarget;
}

void weightedCopy(MaskedImageSP src, int xs, int ys, Vote_type& vote, int xd, int yd, float w)
{
    QRect sz = src->size();
    if (xs>=sz.width() || ys>=sz.height() || src->isMasked(xs, ys))
        return;

    if(xd>=vote.shape()[0] || yd>=vote.shape()[1])
        return;

    QVector<float> pixel = src->getImagePixels(xs, ys);
    for(int i=0; i<src->channelCount(); ++i){
        vote[xd][yd].channel_values[i] += w*pixel.at(i);
    }
    vote[xd][yd].w += w;
}


void NearestNeighborField::ExpectationStep(NearestNeighborFieldSP nnf, bool sourceToTarget, Vote_type& vote, MaskedImageSP source, bool upscale)
{
    int xs,ys,xt,yt;
    //int*** field = nnf->field;
    int R = nnf->patchSize;

    int H = nnf->input->size().height();
    int W = nnf->input->size().width();
    int Ho = nnf->output->size().height();
    int Wo = nnf->output->size().width();

    for (int x=0 ; x<W ; ++x) {
        for (int y=0 ; y<H; ++y) {
            // x,y = center pixel of patch in input

            // xp,yp = center pixel of best corresponding patch in output
            int xp = nnf->field[x][y].x;
            int yp = nnf->field[x][y].y;
            int dp = nnf->field[x][y].distance;

            // similarity measure between the two patches
            float w = nnf->similarity[dp];

            // vote for each pixel inside the input patch
            for ( int dx=-R ; dx<=R; ++dx) {
                for ( int dy=-R ; dy<=R ; ++dy) {

                    // get corresponding pixel in output patch
                    if (sourceToTarget){
                        xs=x+dx;
                        ys=y+dy;
                        xt=xp+dx;
                        yt=yp+dy;
                    }
                    else{
                        xs=xp+dx;
                        ys=yp+dy;
                        xt=x+dx;
                        yt=y+dy;
                    }

                    if (xs<0 || xs>=W || ys<0 || ys>=H || xt<0 || xt>=Wo || yt<0 || yt>=Ho)
                        continue;

                    // add vote for the value
                    if (upscale) {
                        weightedCopy(source, 2*xs,   2*ys,   vote, 2*xt,   2*yt,   w);
                        weightedCopy(source, 2*xs+1, 2*ys,   vote, 2*xt+1, 2*yt,   w);
                        weightedCopy(source, 2*xs,   2*ys+1, vote, 2*xt,   2*yt+1, w);
                        weightedCopy(source, 2*xs+1, 2*ys+1, vote, 2*xt+1, 2*yt+1, w);
                    } else {
                        weightedCopy(source, xs, ys, vote, xt, yt, w);
                    }
                }
            }
        }
    }
}


MaskedImageSP TestClone::patchImage(KisPaintDeviceSP dev, KisPaintDeviceSP devMask, int radius)
{

    Inpaint inpaint(dev, devMask, radius);
    //return inpaint.patch_simple();

    return inpaint.patch();
}

void TestClone::testPatchMatch()
{
    QImage mainImage("/home/eugening/Projects/patch-inpainting/bungee.png");    //TestUtil::fetchDataFileLazy("fill1_main.png"));
    QVERIFY(!mainImage.isNull());

    QImage maskImage("/home/eugening/Projects/patch-inpainting/bungee-mask.png");    //TestUtil::fetchDataFileLazy("fill1_main.png"));
    QVERIFY(!maskImage.isNull());

    KisPaintDeviceSP mainDev = new KisPaintDevice(KoColorSpaceRegistry::instance()->rgb8());
    mainDev->convertFromQImage(mainImage, 0);
    QRect rect = mainDev->exactBounds();

    KisPaintDeviceSP maskDev = new KisPaintDevice(KoColorSpaceRegistry::instance()->alpha8()); //colorSpace(GrayAColorModelID.id(), Integer8BitsColorDepthID.id(), QString()));
    maskImage.invertPixels(QImage::InvertRgba);
    maskDev->convertFromQImage(maskImage, 0);

    QRect rectMask = maskDev->exactBounds();
    //QVERIFY(rect==rectMask);

    KIS_DUMP_DEVICE_2(mainDev, rect, "maindev", "/home/eugening/Projects/img");
    KIS_DUMP_DEVICE_2(maskDev, rect, "maskdev", "/home/eugening/Projects/img");

    MaskedImageSP output = patchImage(mainDev, maskDev, 2);
    output->toPaintDevice(mainDev);
    KIS_DUMP_DEVICE_2(mainDev, output->size(), "output", "/home/eugening/Projects/Out");
}


void TestClone::test(void)
{
    KisSurrogateUndoStore *undoStore = new KisSurrogateUndoStore();

    KisImageSP image = createImage(undoStore);
    KisDocument *doc = KisPart::instance()->createDocument();
    doc->setCurrentImage(image);

    image->initialRefreshGraph();

    KisLayerSP layer = new KisPaintLayer(image, "clone", OPACITY_OPAQUE_U8, image->colorSpace());
    image->addNode(layer, image->root());

    KisPaintDeviceSP dev = layer->paintDevice(); //chld->paintDevice(); //
    KisPainter painter(dev);

//    QScopedPointer<KoCanvasResourceManager> manager(
//        utils::createResourceManager(image, layer, "Basic_circle.kpp"));

//    KisPaintOpPresetSP preset =
//        manager->resource(KisCanvasResourceProvider::CurrentPaintOpPreset).value<KisPaintOpPresetSP>();

//    KisResourcesSnapshotSP resources =
//        new KisResourcesSnapshot(image,
//                                 layer,
//                                 manager.data());
//    resources->setupPainter(&painter);


//    painter.setPaintColor(KoColor(Qt::black, image->colorSpace()));
//    painter.setFillStyle(KisPainter::FillStyle::FillStyleForegroundColor);

//    KisDistanceInformation dist;

//    for (int x = 100; x < 200; x += 5) {
//        KisPaintInformation pi(QPointF(x, x), 1.0);
//        painter.paintAt(pi, &dist);
//    }
//    painter.device()->setDirty(painter.takeDirtyRegion());

//    image->refreshGraph();
//    doc->saveAs(QUrl(QString("/home/eugening/Projects/test.kra")));

    delete doc;
}

void KisCloneOpTest::testClone()
{
    TestClone t;
    //t.test();
    t.testPatchMatch();
}


void KisCloneOpTest::testProjection()
{
    KisDocument *doc = KisPart::instance()->createDocument();
    doc->loadNativeFormat("/home/eugening/Pictures/Krita_Test/Img_20M_3Layer.kra");
//    KisPaintDeviceSP pd = nullptr;

    doc->image()->refreshGraph();
//    QBENCHMARK_ONCE{
//        doc->image()->refreshGraph();
//    }
//    QBENCHMARK_ONCE{
//        pd = doc->image()->projection();
//    }

    //KIS_DUMP_DEVICE_2(proj,proj->exactBounds(),"Img20M","/home/eugening/Projects/Img20M");
    delete doc;
}

QTEST_GUILESS_MAIN(KisCloneOpTest)




//void NearestNeighborField::EM_Step(MaskedImageSP source, MaskedImageSP target, int R, bool upscaled)
//{
//    const KoColorSpace* cs = input->getImageDev()->colorSpace();

//    const QRect& sz = output->size();

//    if (upscaled)
//        R *= 2;

//    //for each pixel in the target image
//    for (int y = 0; y < target->size().height(); y++) {
//        for (int x = 0; x < target->size().width(); x++) {

//            //zero init histogram
//            std::fill(histogram.origin(), histogram.origin() + histogram.num_elements(), 0.);
//            float wsum = 0.;

//            //Estimation step
//            //for all target patches containing the pixel
//            for (int dy = -R; dy <= R; dy++) {
//                for (int dx = -R; dx <= R; dx++) {
//                    //xpt,ypt = center pixel of the target patch
//                    int xpt = x + dx;
//                    int ypt = y + dy;

//                    //get best corrsponding source patch from the NNF
//                    int xst, yst;
//                    float w;
//                    if (!upscaled) {
//                        if (xpt < 0 || xpt >= sz.width()) continue;
//                        if (ypt < 0 || ypt >= sz.height()) continue;
//                        xst = field[xpt][ypt].x;
//                        yst = field[xpt][ypt].y;
//                        w = similarity[field[xpt][ypt].distance];
//                        //printf("%d, ", field[xpt][ypt].distance);
//                    } else {
//                        if (xpt < 0 || xpt >= sz.width()) continue;
//                        if (ypt < 0 || ypt >= sz.height()) continue;
//                        xst = 2 * field[xpt / 2][ypt / 2].x + (xpt % 2);
//                        yst = 2 * field[xpt / 2][ypt / 2].y + (ypt % 2);
//                        w = similarity[field[xpt / 2][ypt / 2].distance];
//                        //printf("%d, ", field[xpt/2][ypt/2].distance);
//                    }

//                    //get pixel corresponding to (x,y) in the source patch
//                    int xs = xst - dx;
//                    int ys = yst - dy;
//                    if (xs < 0 || xs >= sz.width()) continue;
//                    if (ys < 0 || ys >= sz.height()) continue;

//                    //add contribution of the source pixel
//                    if (source->isMasked(xs, ys)) continue;

//                    int colorChan = 0;
//                    for (int chan = 0; chan < cs->channelCount(); chan++) {
//                        if (channels.at(chan)->channelType() != KoChannelInfo::ALPHA) {
//                            quint8 colorValue = source->getImagePixelU8(xs, ys, chan);
//                            histogram[colorChan][colorValue] += w;
//                            colorChan++;
//                        }
//                    }
//                    wsum += w;
//                }
//            }

//            //nnf->debugDumpHistogram("/home/eugening/Projects/","NNF");
//            //no significant contribution : conserve the values from previous target
//            if (wsum < 1.e-2)
//                continue;

//            if (0 && sz.width() > 50) {
//                int colorChan = 0;
//                for (int chan = 0; chan < cs->channelCount(); chan++) {
//                    if (channels.at(chan)->channelType() != KoChannelInfo::ALPHA) {
//                        printf("Chan: %d\n", chan);
//                        printf("[");
//                        for (int i = 0; i < 256; i++) {
//                            printf(" %g,", histogram[colorChan][i]);
//                        }
//                        colorChan++;
//                        printf("]");
//                    }
//                }
//            }
//            //Maximization step
//            //average the contributions of significant pixels (near the median)
//            float lowth = 0.4 * wsum; //low threshold in the CDF
//            float highth = 0.6 * wsum; //high threshold in the CDF
//            int colorChan = 0;
//            QVector<float> channel_values = source->getImagePixels(x, y);
//            for (int chan = 0; chan < channels.size(); chan++) {
//                if (channels.at(chan)->channelType() != KoChannelInfo::ALPHA) {
//                    float cdf = 0;
//                    float contrib = 0;
//                    float wcontrib = 0;

//                    for (int i = 0; i < 256; i++) {
//                        cdf += histogram[colorChan][i];
//                        if (cdf < lowth)
//                            continue;
//                        contrib += i * histogram[colorChan][i];
//                        wcontrib += histogram[colorChan][i];
//                        if (cdf > highth)
//                            break;
//                    }

//                    float v = (wcontrib == 0) ? 0 : contrib / wcontrib;
//                    channel_values[chan] = v / 256.;
//                    colorChan++;
//                }
//            }
//            target->setImagePixels(x, y, channel_values);
//        }
//    }
//}


//void NearestNeighborField::EM_Step(MaskedImageSP source, MaskedImageSP target, int R, bool upscaled){
//    const KoColorSpace* cs = input->getImageDev()->colorSpace();

//    const QRect& sz = source->size();

//    if(upscaled)
//        R *= 2;

//    //for each pixel in the target image
//    for(int y=0; y<target->size().height(); y++){
//        for(int x=0; x<target->size().width(); x++){
//            float wbest = MAX_DIST;
//            int xsbest = 0;
//            int ysbest = 0;

//            //Estimation step
//            //for all target patches containing the pixel
//            for(int dy=-R; dy<=R; dy++){
//                for(int dx=-R; dx<=R; dx++){
//                    //xpt,ypt = center pixel of the target patch
//                    int xpt = x+dx;
//                    int ypt = y+dy;

//                    //get best corrsponding source patch from the NNF
//                    int xst, yst;
//                    float w;
//                    if(!upscaled){
//                        if(xpt < 0 || xpt >= sz.width()) continue;
//                        if(ypt < 0 || ypt >= sz.height()) continue;
//                        xst=field[xpt][ypt].x;
//                        yst=field[xpt][ypt].y;
//                        w = field[xpt][ypt].distance;
//                        //printf("%d, ", field[xpt][ypt].distance);
//                    } else{
//                        if(xpt < 0 || xpt >= sz.width()) continue;
//                        if(ypt < 0 || ypt >= sz.height()) continue;
//                        xst=2*field[xpt/2][ypt/2].x + (xpt%2);
//                        yst=2*field[xpt/2][ypt/2].y + (ypt%2);
//                        w = field[xpt/2][ypt/2].distance;
//                        //printf("%d, ", field[xpt/2][ypt/2].distance);
//                    }

//                    //get pixel corresponding to (x,y) in the source patch
//                    int xs = xst-dx;
//                    int ys = yst-dy;
//                    if(xs < 0 || xs >= sz.width()) continue;
//                    if(ys < 0 || ys >= sz.height()) continue;

//                    //add contribution of the source pixel
//                    if( source->isMasked(xs, ys) ) continue;

//                    if(w < wbest){
//                        xsbest = xs;
//                        ysbest = ys;
//                        wbest = w;
//                    }
//                }
//            }

//            //nnf->debugDumpHistogram("/home/eugening/Projects/","NNF");
//            //no better choice : conserve the values from previous target
//            if(wbest >= MAX_DIST)
//                continue;

//            QVector<float> channel_values = source->getImagePixels(xsbest, ysbest);
//            target->setImagePixels(x, y, channel_values);
//        }
//    }
//}


//void downsampleRow(KisHLineConstIteratorNG& imageIt0, KisHLineConstIteratorNG& imageIt1,
//                   KisHLineConstIteratorNG& maskIt0, KisHLineConstIteratorNG& maskIt1,
//                   KisHLineIteratorNG& dstImageIt, KisHLineIteratorNG& dstMaskIt)
//{
//    bool ret = true;

//    const KoColorSpace* cs = imageDev->colorSpace();
//    //average 4 pixels
//    const quint8* pixels[4];
//    static const qint16 weights[4] = {64, 64, 64, 63}; //weights sum to 255 for averaging
//    while (ret) {
//        //handle mask?
//        pixels[0] = imageIt0.oldRawData();
//        imageIt0.nextPixel();
//        pixels[1] = imageIt0.oldRawData();
//        ret &= imageIt0.nextPixel();
//        pixels[2] = imageIt1.oldRawData();
//        imageIt1.nextPixel();
//        pixels[3] = imageIt1.oldRawData();
//        ret &= imageIt1.nextPixel();

//        cs->mixColorsOp()->mixColors(pixels, weights, 4, dstImageIt.rawData());
//        dstImageIt.nextPixel();

//        pixels[0] = maskIt0.oldRawData();
//        maskIt0.nextPixel();
//        pixels[1] = maskIt0.oldRawData();
//        ret &= maskIt0.nextPixel();
//        pixels[2] = maskIt1.oldRawData();
//        maskIt1.nextPixel();
//        pixels[3] = maskIt1.oldRawData();
//        ret &= maskIt1.nextPixel();

//        maskDev->colorSpace()->mixColorsOp()->mixColors(pixels, weights, 4, dstMaskIt.rawData());
//        dstMaskIt.nextPixel();
//    }
//}

//        KoDummyUpdater updater;
//        KisTransformWorker worker(imageDev, 1. / 2., 1. / 2., 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
//                                  &updater, KisFilterStrategyRegistry::instance()->value("Bicubic"));
//        worker.run();

//        KisTransformWorker workerMask(maskDev, 1. / 2., 1. / 2., 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
//                                      &updater, KisFilterStrategyRegistry::instance()->value("Box"));
//        workerMask.run();
//        cacheEverything();


//QRect sz = size();

//KisSharedPtr<MaskedImage> scaledImage = this->initialize();

//KoDummyUpdater updater;
//KisTransformWorker worker(scaledImage->getImageDev(), (double)xsize / sz.width(), (double)ysize / sz.height(), 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
//                          &updater, KisFilterStrategyRegistry::instance()->value("Bicubic"));
//worker.run();

//KisTransformWorker workerMask(scaledImage->getMaskDev(), (double)xsize / sz.width(), (double)ysize / sz.height(), 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
//                              &updater, KisFilterStrategyRegistry::instance()->value("Box"));
//workerMask.run();

//scaledImage->cacheEverything();
//return scaledImage;
