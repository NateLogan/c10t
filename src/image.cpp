// Distributed under the BSD License, see accompanying LICENSE.txt
// (C) Copyright 2010 John-John Tedro et al.
#include "image.h"
#include "global.h"

#include <boost/numeric/conversion/cast.hpp>

#include <algorithm>
#include <assert.h>
#include <stdint.h>
#include <limits>

#include <png.h>

void image_operations::add_pixel(int x, int y, color &c) {
  if (c.is_invisible()) {
    return;
  }
  
  if (!(x >= 0)) { return; }
  if (!(y >= 0)) { return; }
  if (!(x < maxx)) { return; }
  if (!(y < maxy)) { return; }
  
  image_operation oper;
  
  oper.x = (uint16_t)x;
  oper.y = (uint16_t)y;
  //oper.order = ++order;
  oper.c = c;
  
  if (!oper.c.is_transparent()) {
    size_t p = oper.x + oper.y * maxx;
    
    if (lookup[p]) {
      return;
    }
    
    lookup[p] = true;
  }
  
  operations.push_back(oper);
}

void memory_image::set_pixel(size_t x, size_t y, color &c) {
  if (!(x < get_width())) { return; }
  if (!(y < get_height())) { return; }
  c.write(this->colors + get_offset(x, y));
}

void memory_image::get_pixel(size_t x, size_t y, color &c){
  if (!(x < get_width())) { return; }
  if (!(y < get_height())) { return; }
  c.read(this->colors + get_offset(x, y));
}

void memory_image::get_line(size_t y, size_t offset, size_t width, color* c) {
  if (!(y < get_height())) { return; }
  if (!(offset < get_width())) { return; }
  if (!(width + offset < get_width())) { width = get_width() - offset; }
  
  memcpy(c, this->colors + get_offset(offset, y), width * sizeof(color));
}

void memory_image::blend_pixel(size_t x, size_t y, color &c){
  color o;
  get_pixel(x, y, o);
  
  if (o.is_invisible()) {
    set_pixel(x, y, c);
    return;
  }
  
  o.blend(c);
  set_pixel(x, y, o);
}

void image_base::fill(color &q){
  for (size_t x = 0; x < get_width(); x++) {
    for (size_t y = 0; y < get_height(); y++) {
      set_pixel(x, y, q);
    }
  }
}

void image_base::composite(int xoffset, int yoffset, image_operations &img) {
  std::vector<image_operation>::size_type i = img.operations.size();
  
  while (i--) {
    image_operation op = img.operations[i];
    
    color base;
    get_pixel(xoffset + op.x, yoffset + op.y, base);
    base.blend(op.c);
    set_pixel(xoffset + op.x, yoffset + op.y, base);
  }
}

void image_base::composite(int xoffset, int yoffset, image_base &img) {
  if (!(xoffset >= 0)) { return; }
  if (!(yoffset >= 0)) { return; }

  size_t s_xoffset = boost::numeric_cast<size_t>(xoffset);
  size_t s_yoffset = boost::numeric_cast<size_t>(yoffset);
  
  if (!(s_xoffset + img.get_width() <= w)) { return; }
  if (!(s_yoffset + img.get_height() <= h)) { return; }
  
  color hp;
  color base;
  
  for (size_t x = 0; x < img.get_width(); x++) {
    for (size_t y = 0; y < img.get_height(); y++) {
      get_pixel(xoffset + x, s_yoffset + y, base);
      img.get_pixel(x, y, hp);
      base.blend(hp);
      set_pixel(xoffset + x, s_yoffset + y, base);
    }
  }
}

void image_base::safe_composite(int xoffset, int yoffset, image_base &img) {
  if (xoffset < 0) return;
  if (xoffset + img.get_width() > w) return;
  if (yoffset < 0) return;
  if (yoffset + img.get_height() > h) return;
  composite(xoffset, yoffset, img);
}

void image_base::safe_blend_pixel(size_t x, size_t y, color &c) {
  if (x >= w) return;
  if (y >= h) return;
  blend_pixel(x, y, c);
}

bool image_base::save_png(const std::string path, const char *title, progress_c progress_c_cb)
{
  bool ret = true;
  
  FILE *fp;
  png_structp png_ptr = NULL;
  png_infop info_ptr = NULL;
  png_bytep row = NULL;
  
  if (path.compare("-") == 0) {
    fp = stdout;
  } else {
    fp = fopen(path.c_str(), "wb");
    
    if (fp == NULL) {
       ret = false;
       goto finalise;
    }
  }
  
  png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
 
  if (png_ptr == NULL) {
     ret = false;
     goto finalise;
  }

  info_ptr = png_create_info_struct(png_ptr);

  if (info_ptr == NULL) {
     ret = false;
     goto finalise;
  }

  if (setjmp(png_jmpbuf(png_ptr))) {
     ret = false;
     goto finalise;
  }

  png_init_io(png_ptr, fp);

  png_set_IHDR(png_ptr, info_ptr, get_width(), get_height(),
        8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
   
  if (title != NULL) {
     png_text title_text;
     title_text.compression = PNG_TEXT_COMPRESSION_NONE;
     title_text.key = (char *)"Title";
     title_text.text = (char *)title;
     png_set_text(png_ptr, info_ptr, &title_text, 1);
  }

  png_write_info(png_ptr, info_ptr);

  row = (png_bytep) malloc(4 * get_width() * sizeof(png_byte));
  
  for (size_t y = 0; y < get_height(); y++) {
    if (progress_c_cb != NULL) progress_c_cb(y, get_height());
    get_line(y, reinterpret_cast<color*>(row));
    png_write_row(png_ptr, row);
  }
  
  if (progress_c_cb != NULL) progress_c_cb(get_height(), get_height());
  
  png_write_end(png_ptr, NULL);

finalise:
  if (fp != NULL) {
    fclose(fp);
  }
   
  if (info_ptr != NULL) {
    png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);
  }

  if (png_ptr != NULL) {
    png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
  }

  if (row != NULL) {
    free(row);
  }
  
  return ret;
}


void cached_image::set_pixel(size_t x, size_t y, color& c) {
  if (!(x < get_width())) { return; }
  if (!(y < get_height())) { return; }
  fs.seekp(get_offset(x, y), std::ios::beg);
  fs.write(reinterpret_cast<char*>(&c), sizeof(color));
}

void cached_image::get_pixel(size_t x, size_t y, color& c) {
  if (!(x < get_width())) { return; }
  if (!(y < get_height())) { return; }
  fs.seekg(get_offset(x, y), std::ios::beg);
  fs.read(reinterpret_cast<char*>(&c), sizeof(color));
}

void cached_image::get_line(size_t y, size_t offset, size_t width, color* c) {
  if (!(y < get_height())) { return; }
  if (!(offset < get_width())) { return; }
  if (!(width + offset < get_width())) { width = get_width() - offset; }
  fs.seekg(get_offset(0, y), std::ios::beg);
  fs.read(reinterpret_cast<char*>(c), sizeof(color) * width);
}

void cached_image::blend_pixel(size_t x, size_t y, color &c){
  // do nothing if color is invisible
  if (c.is_invisible()) {
    return;
  }
  
  size_t s = (x + y * get_width()) % buffer_size;
  
  icache* ic = &buffer[s];
  
  // cache hit
  if (ic->is_set()) {
    // cache hit, but wrong coordinates - flush pixel to file
    if (ic->x != x || ic->y != y)  {
      set_pixel(ic->x, ic->y, ic->c);
      ic->c = c;
      ic->x = x;
      ic->y = y;
      return;
    }
    
    ic->c.blend(c);
  }
  // cache miss - just set the cache
  else {
    ic->c = c;
    ic->x = x;
    ic->y = y;
  }
}

std::map<point2, image_base*> image_split(image_base* base, int pixels) {
  std::map<point2, image_base*> map;
  
  for (size_t w = 0, px = 0; w < base->get_width(); w += pixels, px++) {
    for (size_t h = 0, py = 0; h < base->get_height(); h += pixels, py++) {
      point2 p(px, py);
      map[p] = new virtual_image(pixels, pixels, base, w, h);
    }
  }
  
  return map;
}

/* align the memory cache with the actual image */
/*void cached_image::align(int xp, int yp, int xw, int yw)
{
  for (int y = yp; y < yp + yw; y++) {
    for (int x = xp; x < xp + xw; x++) {
      size_t s = (x + y * get_width()) % buffer_size;
      icache ic = buffer[s];
    }
  }
}
*/
