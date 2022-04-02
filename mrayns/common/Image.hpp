//
// Created by wyz on 2022/2/23.
//
#pragma once
#include "Define.hpp"
#include "MathTypes.hpp"
#include <stdexcept>
MRAYNS_BEGIN

class Image{
  public:
    Image():w(0),h(0),s(0),d(nullptr){

    }
    Image(int w,int h):w(w),h(h),s(w*h){
        d = static_cast<RGBA*>(::operator new(sizeof(RGBA)*s));
        for(int i =0;i<s;i++)
            new(d+i) RGBA(0);
    }
    Image(const Image& other):Image(other.w,other.h){
        for(int i = 0;i<s;i++)
            d[i] = other.d[i];
    }
    Image(Image&& other) noexcept:w(other.w),h(other.h),s(other.s),d(other.d){

    }
    Image& operator=(const Image& other){
        this->destroy();
        new(this) Image(other);
        return *this;
    }
    Image& operator=(Image&& other) noexcept{
        this->destroy();
        new(this) Image(std::move(other));
        return *this;
    }

    RGBA& operator()(int x,int y) noexcept{
        return d[toLinearIndex(x,y)];
    }
    const RGBA& operator()(int x,int y) const noexcept {
        return d[toLinearIndex(x,y)];
    }
    RGBA& at(int x,int y) {
        if(x>=w || x<0 || y>=h || y<0)
            throw std::out_of_range("Image at out of range");
        return d[toLinearIndex(x,y)];
    }
    int toLinearIndex(int x,int y) const {
        return y*w+x;
    }

    bool saveToFile(const std::string& name);

    const RGBA* data() const{ return d; }
    size_t size() const {return height() * pitch();}
    RGBA* data() { return d; }
    int pitch() const { return sizeof(RGBA)*w; }
    int width() const { return w; }
    int height() const { return h; }
    bool isValid() const{ return d;}
    void destroy(){
        w = h = s = 0;
        if(d){
            ::operator delete(d);
            d = nullptr;
        }
    }
  private:
    int w,h,s;
    RGBA* d;
};

MRAYNS_END