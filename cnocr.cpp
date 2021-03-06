#include "cnocr.h"
#include <algorithm>
#include <locale>
cnocr::cnocr(/* args */)
{
    std::locale lc("zh_CN.UTF-8");
    std::locale::global(lc);
    //载入中文字符
    wchar_t buffer[100];
    std::wifstream ctc_char(ctc_path);
    while (!ctc_char.eof() )
    {
        ctc_char.getline(buffer,100);
        ctc_data.push_back(buffer[0]);
    }
}

std::vector<std::wstring> cnocr::ocr(std::string path){
    auto inimg =cv::imread(path,cv::IMREAD_GRAYSCALE);
    cv::Mat outimg=inimg;
    std::vector<std::wstring> res;
    auto imgcol=outimg.cols;
    auto imgrow=outimg.rows;
    if (std::min(imgrow,imgcol) < 2){
        return res;
    }
    if (cv::sum(outimg.col(0))[0] < 145) // 把黑底白字的图片对调为白底黑字
    {
        outimg=255-outimg;
    }
    auto imgs=line_split(outimg);
    res=ocr_for_single_lines(imgs);
    return res;
}
std::vector<std::wstring> cnocr::ocr(cv::Mat& inimg){
    cv::Mat outimg;
    cv::cvtColor(inimg,outimg,cv::COLOR_RGB2GRAY);
    std::vector<std::wstring> res;
    auto imgcol=outimg.cols;
    auto imgrow=outimg.rows;
    if (std::min(imgrow,imgcol) < 2){
        return res;
    }
    if (cv::sum(outimg.col(0))[0] < 145) // 把黑底白字的图片对调为白底黑字
    {
        outimg=255-outimg;
    }
    auto imgs=line_split(outimg);
    res=ocr_for_single_lines(imgs);
    return res;
}

std::vector<cv::Mat> cnocr::line_split(cv::Mat& inimg){
    std::vector<cv::Mat> list;
    auto imgcol=inimg.cols;
    auto imgrow=inimg.rows;
    auto bij=inimg.row(0);
    for (int i=0;i<imgrow;i++){
        *(bij.data+i)=255;
    }
    int lineforchar=0;
    int lineforcharstart=0;
    for (int i=0;i<imgrow;i++){
        cv::Mat res=bij-inimg.row(i);
        if (*std::max_element(res.begin<uchar>(),res.end<uchar>())>100){
            if (lineforchar+1==i){
                lineforchar++;
            }
            else{
                if (lineforchar-lineforcharstart>7){
                    int start=lineforcharstart;
                    int end=lineforchar;
                    if (start>0){
                        start-=1;
                    }
                    if (end<imgcol-1){
                        end+=1;
                    }
                    list.push_back(inimg(cv::Rect(0,start,inimg.cols,end-start)));
                }
                lineforcharstart=i;
                lineforchar=i;
            }
        }
    }
    if (lineforchar-lineforcharstart>5){
        int start=lineforcharstart;
        int end=lineforchar;
        if (start>0){
            start-=1;
        }
        if (end<imgcol-1){
            end+=1;
        }
        list.push_back(inimg(cv::Rect(0,start,inimg.cols,end-start)));
    }
    return list;
}
void softmax(cv::Mat &input){
    for (int i=0;i<input.rows;i++){
        auto ncdata=input.row(i);
        double t=(*std::max_element(ncdata.begin<float>(),ncdata.end<float>()));
        cv::exp(ncdata-t,ncdata);
        double t1=cv::sum(ncdata)[0];
        ncdata=ncdata/t1;
    }
}

template<class ForwardIterator>
inline size_t argmax(ForwardIterator first, ForwardIterator last)
{
    return std::distance(first, std::max_element(first, last));
}
std::vector<uint16_t> vargmax(cv::Mat input) {
    std::vector<uint16_t> res;
    for (int i=0;i<input.rows;i++){
        auto ncdata=input.row(i);
        res.push_back(argmax(ncdata.begin<float>(),ncdata.end<float>()));
    }
    return res;
}
std::vector<std::wstring> cnocr::ocr_for_single_lines(std::vector<cv::Mat>& imgs){
    std::vector<std::wstring>res;
    if (imgs.size()==0){
        return res;
    }
    std::cout<<"have lines x for ocr:"<<imgs.size()<<std::endl;
    //res = self.rec_model.recognize(img_list, batch_size=batch_size)
    for (auto img:imgs){
        auto imgcol=img.cols;
        auto imgrow=img.rows;
        std::cout <<"imgSize:"<<imgcol<<","<<imgrow<<std::endl;
        //等比例放图像至高度32
        auto imgmat=img;
        float ratio=imgrow/32.0;
        cv::Size sz((int)(imgcol/ratio),32);
        cv::Mat imgresize(sz,CV_8UC1);
        cv::resize(imgmat,imgresize,sz);
        //调用模型
        long long input_height=imgresize.size[0];
        long long input_width=imgresize.size[1];
        //run_ort_trt(input_width,input_height*input_width,imgresize.data);
        std::vector<void *>ret_data=modle.run(input_width,input_height*input_width,imgresize.data);
        //img.transpose(2,0,1); 将数据转换
        ////auto ncdata=nc::empty<double>(*(int64_t*)ret_data[0],6674);
        ////for (size_t i=0;i<*(int64_t*)ret_data[0]*6674;i++){
        ////        *(ncdata.data()+i)=*((float*)ret_data[1]+i);
        ////}
        auto ncdata=cv::Mat(*(int64_t*)ret_data[0],6674,CV_32FC1,(float*)ret_data[1]);
        softmax(ncdata);
        //probs = F.softmax(logits.permute(0, 2, 1), dim=1)
        //auto probs=nc::special::softmax(ncdata,nc::Axis::COL);
        //best_path = torch.argmax(probs, dim=1)  # [N, T]
        //auto best_path=nc::argmax(probs,nc::Axis::COL);
        std::vector<uint16_t> best_path=vargmax(ncdata);
        //cv::reduce(ncdata, best_path, 0, cv::REDUCE_AVG);\

        res.push_back(ctc_best(best_path));
        
    }
    return res;
}
std::wstring cnocr::ctc_best(std::vector<uint16_t> data){
    std::wstring res;
    std::vector<uint32_t> vui;//消除重复的
    std::cout<<data.size()<<std::endl;
    for (auto i=data.begin();i!=data.end();i++){
        if (vui.size()!=0){
            if (vui[vui.size()-1]!=(uint32_t)*i){
                vui.push_back((uint32_t)*i);
            }
        }
        else{
            vui.push_back((uint32_t)*i);
        }
    }
    for (auto i:vui){
        if (i<6673){
            res.push_back(ctc_data[i]);
        }
    }
    return res;
}
cnocr::~cnocr()
{
}