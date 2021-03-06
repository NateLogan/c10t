// Distributed under the BSD License, see accompanying LICENSE.txt
// (C) Copyright 2010 John-John Tedro et al.
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>

#include <errno.h>

#include <sstream>
#include <string>
#include <list>
#include <iostream>
#include <iomanip>
#include <fstream>

#include <boost/algorithm/string.hpp>
#include <boost/ptr_container/ptr_list.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/tokenizer.hpp>
#include <boost/lexical_cast.hpp>

#include "config.h"

#include "threads/threadworker.h"
#include "2d/cube.h"

#include "global.h"
#include "level.h"
#include "image.h"
#include "blocks.h"
#include "fileutils.h"
#include "world.h"
#include "players.h"
#include "text.h"
#include "marker.h"
#include "json.h"
#include "warps.h"

using namespace std;
namespace fs = boost::filesystem;

stringstream error;
const uint8_t ERROR_BYTE = 0x01;
const uint8_t RENDER_BYTE = 0x10;
const uint8_t COMP_BYTE = 0x20;
const uint8_t IMAGE_BYTE = 0x30;
const uint8_t PARSE_BYTE = 0x40;
const uint8_t END_BYTE = 0xF0;

void cout_progress_n(int i, int all) {
  if (i == all) {
    cout << setw(6) << "done!" << endl;
  }
  else {
    if (i % 50 == 0 && i > 0) {
      cout << "." << flush;
      
      if (i % 1000 == 0) {
        cout << setw(8) << i << " " << (i * 100) / all << "%" << endl;
      }
    }
  } 
}

void cout_progress_ionly_n(int i, int all) {
  if (all == 1) {
    cout << setw(6) << "done!" << endl;
  }
  else if (i % 50 == 0 && i > 0) {
    cout << "." << flush;
    
    if (i % 1000 == 0) {
      cout << setw(8) << i << " ?%" << endl;
    }
  } 
}

inline void cout_progress_ionly_b(const uint8_t type, int part, int whole) {
  cout << hex << std::setw(2) << setfill('0') << static_cast<int>(type);
  
  if (whole == 1) {
    cout << hex << std::setw(2) << setfill('0') << static_cast<int>(2) << flush;
  }
  else if (part % 1000 == 0) {
    cout << hex << std::setw(2) << setfill('0') << static_cast<int>(1) << flush;
  }
  else {
    cout << hex << std::setw(2) << setfill('0') << static_cast<int>(0) << flush;
  }
}

inline void cout_progress_b(const uint8_t type, int part, int whole) {
  uint8_t b = ((part * 0xff) / whole);
  cout << hex << std::setw(2) << setfill('0') << static_cast<int>(type)
       << hex << std::setw(2) << setfill('0') << static_cast<int>(b) << flush;
}

void cout_progress_b_parse(int i, int all) {
  cout_progress_ionly_b(PARSE_BYTE, i, all);
}

void cout_progress_b_render(int i, int all) {
  cout_progress_b(RENDER_BYTE, i, all);
}

void cout_progress_b_image(int i, int all) {
  cout_progress_b(IMAGE_BYTE, i, all);
}

inline void cout_error(const string& message) {
  cout << hex << std::setw(2) << setfill('0') << static_cast<int>(ERROR_BYTE)
       << hex << message << flush;
}

inline void cout_end() {
  cout << hex << std::setw(2) << setfill('0') << static_cast<int>(END_BYTE) << flush;
}

/*
 * Store part of a level rendered as a small image.
 *
 * This will allow us to composite the entire image later and calculate sizes then.
 */
struct render_result {
  int xPos, zPos;
  fs::path path;
  boost::shared_ptr<level_file> level;
  
  boost::shared_ptr<image_operations> operations;
};

struct render_job {
  int xPos, zPos;
  fs::path path;
  boost::shared_ptr<level_file> level;
};

class Renderer : public threadworker<render_job, render_result> {
public:
  settings_t& s;
  
  Renderer(settings_t& s, int n) : threadworker<render_job, render_result>(n), s(s) {
  }
  
  render_result work(render_job job) {
    level_file* level = job.level.get();
    
    level->load_file(job.path);
    
    render_result p;
    
    p.path = job.path;
    p.level = job.level;
    p.xPos = job.xPos;
    p.zPos = job.zPos;
    
    if (level->grammar_error) {
      return p;
    }
    
    if (!level->islevel) {
      return p;
    }
    
    switch (s.mode) {
    case Top:           p.operations = level->get_image(s); break;
    case Oblique:       p.operations = level->get_oblique_image(s); break;
    case Isometric:     p.operations = level->get_isometric_image(s); break;
    case ObliqueAngle:  p.operations = level->get_obliqueangle_image(s); break;
    }
    
    return p;
  }
};

inline void calc_image_width_height(settings_t& s, world_info& world, size_t &image_width, size_t &image_height) {
  int diffx = world.max_x - world.min_x;
  int diffz = world.max_z - world.min_z;
  
  Cube c((diffx + 1) * mc::MapX, mc::MapY, (diffz + 1) * mc::MapZ);
  
  switch (s.mode) {
  case Top:
    c.get_top_limits(image_width, image_height);
    break;
  case Oblique:
    c.get_oblique_limits(image_width, image_height);
    break;
  case Isometric:
    c.get_isometric_limits(image_width, image_height);
    break;
  case ObliqueAngle:
    // yes, these are meant to be flipped
    c.get_obliqueangle_limits(image_width, image_height);
    break;
  }
}

inline void calc_image_partial(settings_t& s, render_result &p, image_base *all, world_info &world, int image_width, int image_height) {
  size_t diffx = world.max_x - world.min_x;
  size_t diffz = world.max_z - world.min_z;

  size_t posx = p.xPos - world.min_x;
  size_t posz = p.zPos - world.min_z;
  
  Cube c(diffx * mc::MapX, mc::MapY, diffz * mc::MapZ);
  size_t x, y;
  
  point pos(posx * mc::MapX, mc::MapY, posz * mc::MapZ);
  
  switch (s.mode) {
    case Top:           c.project_top(pos, x, y);           break;
    case Oblique:       c.project_oblique(pos, x, y);       break;
    case ObliqueAngle:  c.project_obliqueangle(pos, x, y);  break;
    case Isometric:     c.project_isometric(pos, x, y);     break;
  }
  
  /*std::cout << "diff-xy: " << diffx << " " << diffz << std::endl;
  std::cout << "pos-xy: " << posx << " " << posz << std::endl;
  std::cout << "xy: " << x << " " << y << std::endl;*/
  
  all->composite(x, y, *p.operations);
}

inline void write_markers(settings_t& s, image_base *all, world_info &world, boost::ptr_vector<marker>& markers) {
  int diffx = (world.max_x - world.min_x) * mc::MapX;
  int diffz = (world.max_z - world.min_z) * mc::MapZ;
  int min_z = world.min_z * mc::MapZ;
  int min_x = world.min_x * mc::MapX;
  
  Cube c(diffx + mc::MapX, mc::MapY, diffz + mc::MapZ);
  
  boost::ptr_vector<marker>::iterator it;

  json::array array;
  
  for (it = markers.begin(); it != markers.end(); it++) {
    marker m = *it;
    
    int p_x = m.x, p_y = m.y, p_z = m.z;
    
    transform_world_xz(p_x, p_z, s.rotation);
    
    point pos(p_x - min_x, p_y, p_z - min_z);

    size_t x, y;
    
    switch (s.mode) {
      case Top:           c.project_top(pos, x, y);           break;
      case Oblique:       c.project_oblique(pos, x, y);       break;
      case ObliqueAngle:  c.project_obliqueangle(pos, x, y);  break;
      case Isometric:     c.project_isometric(pos, x, y);     break;
    }

    json::object o;
    
    o["text"] = m.text;
    o["type"] = m.type;

    // the projected coordinates
    o["x"] = x;
    o["y"] = y;
    
    // the real coordinates
    o["X"] = m.x;
    o["Y"] = m.y;
    o["Z"] = m.z;
    
    array.push(o);
  }
  
  std::ofstream of(s.write_markers_path.string().c_str());
  of << array;
  // don't bother to check for errors right now, but could be done using the "fail" accessor.
}

inline void overlay_markers(settings_t& s, image_base *all, world_info &world, boost::ptr_vector<marker>& markers) {
  int diffx = (world.max_x - world.min_x) * mc::MapX;
  int diffz = (world.max_z - world.min_z) * mc::MapZ;
  int min_z = world.min_z * mc::MapZ;
  int min_x = world.min_x * mc::MapX;
  
  Cube c(diffx + mc::MapX, mc::MapY, diffz + mc::MapZ);
  
  memory_image positionmark(5, 5);
  positionmark.fill(s.ttf_color);
  
  boost::ptr_vector<marker>::iterator it;
  
  for (it = markers.begin(); it != markers.end(); it++) {
    marker m = *it;

    int p_x = m.x, p_y = m.y, p_z = m.z;
    
    transform_world_xz(p_x, p_z, s.rotation);
    point pos(p_x - min_x, p_y, p_z - min_z);

    size_t x, y;
    
    switch (s.mode) {
      case Top:           c.project_top(pos, x, y);           break;
      case Oblique:       c.project_oblique(pos, x, y);       break;
      case ObliqueAngle:  c.project_obliqueangle(pos, x, y);  break;
      case Isometric:     c.project_isometric(pos, x, y);     break;
    }
    
    m.font.draw(*all, m.text, x + 5, y);
    all->safe_composite(x - 3, y - 3, positionmark);
  }
}

bool do_one_world(settings_t &s, world_info& world, players_db& pdb, warps_db& wdb, const string& output) {
  if (s.debug) {
    cout << "world_info" << endl;
    cout << "  min_x: " << world.min_x << endl;
    cout << "  max_x: " << world.max_x << endl;
    cout << "  min_z: " << world.min_z << endl;
    cout << "  max_z: " << world.max_z << endl;
    cout << "  levels: " << world.levels.size() << endl;
    cout << "  chunk pos: " << world.chunk_x << "x" << world.chunk_y << endl;
  }
  
  size_t i_w = 0;
  size_t i_h = 0;
  
  void (*progress_c)(int part, int all) = NULL;
  
  if (!s.silent) {
    progress_c = cout_progress_n;
  }
  
  // calculate i_w / i_h
  calc_image_width_height(s, world, i_w, i_h);
  
  size_t mem_x = i_w * i_h * 4 * sizeof(uint8_t);
  float mem;
  float mem_x_r;
  
  if (mem_x > s.memory_limit) {
    mem = (float)(s.memory_limit) / 1000000.0f; 
    mem_x_r = (float)(mem_x) / 1000000.0f; 
    
    if (!s.silent) cout << output << ": "
         << i_w << "x" << i_h << " "
         << "~" << mem << " MB (" << mem_x_r << "MB cached at " << s.cache_file << ")... " << endl;
  } else {
    mem = (float)(i_w * i_h * 4 * sizeof(uint8_t)) / 1000000.0f; 
  
    if (!s.silent) cout << output << ": "
         << i_w << "x" << i_h << " "
         << "~" << mem << " MB... " << endl;
  }
  
  image_base *all;
  
  if (mem_x > s.memory_limit) {
    try {
      if (!s.silent) cout << "Building cache... " << flush;
      all = new cached_image(s.cache_file.c_str(), i_w, i_h, s.memory_limit / sizeof(icache));
      if (!s.silent) cout << "done!" << endl;
    } catch(std::ios::failure& e) {
      error << strerror(errno) << ": " << s.cache_file;
      return false;
    }
  }
  else {
    all = new memory_image(i_w, i_h);
  }
  
  Renderer renderer(s, s.threads);
  renderer.start();
  unsigned int world_size = world.levels.size();
  
  std::list<level>::iterator lvlit = world.levels.begin();
  
  unsigned int lvlq = 0;
  unsigned int i;

  std::vector<light_marker> light_markers;
  
  if (s.binary) {
    progress_c = cout_progress_b_render;
  }
  
  for (i = 0; i < world_size; i++) {
    if (lvlq == 0) {
      for (; lvlq < s.threads * 4 && lvlit != world.levels.end(); lvlq++) {
        level l = *lvlit;
        
        fs::path path = world.get_level_path(l);
        
        if (s.debug) {
          cout << "using file: " << path << endl;
        }
        
        render_job job;
        job.level.reset(new level_file(s));
        job.path = path;
        job.xPos = l.xPos;
        job.zPos = l.zPos;
        
        renderer.give(job);
        lvlit++;
      }
    }
    
    --lvlq;
    
    render_result p = renderer.get();

    boost::shared_ptr<level_file> level(p.level);
    
    if (level->grammar_error) {
      if (s.require_all) {
        error << "Parser Error: " << p.path.string() << " at (uncompressed) byte " << level->grammar_error_where
          << " - " << level->grammar_error_why;
        
        // effectively join all worker threads and prepare for exit
        renderer.join();
        return false;
      }
      
      if (!s.silent) {
        cout << "Ignoring unparseable file: " << p.path << " - " << level->grammar_error_why << endl;
        continue;
      }
    }
    
    if (!level->islevel) {
      if (s.debug) {
        cout << "Ignoring file not a level chunk: " << p.path << endl;
      }
      
      continue;
    }
    
    if (progress_c != NULL) progress_c(i, world_size);
    
    if (level->markers.size() > 0) {
      if (s.debug) { cout << "Found " << level->markers.size() << " signs"; };
      light_markers.insert(light_markers.end(), level->markers.begin(), level->markers.end());
    }
    
    try {
      calc_image_partial(s, p, all, world, i_w, i_h);
    } catch(std::ios::failure& e) {
      error << strerror(errno) << ": " << s.cache_file;
      renderer.join();
    }
  }
  
  if (progress_c != NULL) progress_c(world_size, world_size);
  
  renderer.join();

  boost::ptr_vector<marker> markers;

  bool show_markers =
    s.show_players
    || s.show_signs
    || s.show_coordinates
    || s.show_warps;
  
  if (show_markers) {
    fs::path ttf_path(s.ttf_path);
    
    if (!fs::is_regular_file(ttf_path)) {
      error << "ttf_path - not a file: " << ttf_path;
      return false;
    }
    
    text::font_face font(ttf_path.string(), s.ttf_size, s.ttf_color);
    
    if (s.show_players) {
      text::font_face player_font = font;
      
      if (s.has_player_color) {
        player_font.set_color(s.player_color);
      }
      
      std::vector<player>::iterator plit = pdb.players.begin();
      
      /* initial code for projecting players */
      for (; plit != pdb.players.end(); plit++) { 
        player p = *plit;
        
        if (p.zPos / mc::MapZ < s.min_z) continue;
        if (p.zPos / mc::MapZ > s.max_z) continue;
        if (p.xPos / mc::MapX < s.min_x) continue;
        if (p.xPos / mc::MapX > s.max_x) continue;
        
        marker *m = new marker(p.name, "player", player_font, p.xPos, p.yPos, p.zPos);
        markers.push_back(m);
      }
    }
    
    if (s.show_signs && light_markers.size() > 0) {
      text::font_face sign_font = font;
      
      if (s.has_sign_color) {
        sign_font.set_color(s.sign_color);
      }
      
      std::vector<light_marker>::iterator lmit = light_markers.begin();
      
      for (; lmit != light_markers.end(); lmit++) {
        light_marker lm = *lmit;
        
        if (!s.show_signs_filter.empty() && lm.text.find(s.show_signs_filter) == string::npos) {
          continue;
        }
        
        marker *m = new marker(lm.text, "sign", sign_font, lm.x, lm.y, lm.z);
        markers.push_back(m);
      }
    }
    
    if (s.show_coordinates) {
      text::font_face coordinate_font = font;
      
      if (s.has_coordinate_color) {
        coordinate_font.set_color(s.coordinate_color);
      }
      
      for (lvlit = world.levels.begin(); lvlit != world.levels.end(); lvlit++) {
        level l = *lvlit;
        if (l.zPos - 4 < world.min_z) continue;
        if (l.zPos + 4 > world.max_z) continue;
        if (l.xPos - 4 < world.min_x) continue;
        if (l.xPos + 4 > world.max_x) continue;
        if (l.zPos % 10 != 0) continue;
        if (l.xPos % 10 != 0) continue;
        std::stringstream ss;
        ss << "(" << l.xPos * mc::MapX << ", " << l.zPos * mc::MapZ << ")";
        marker *m = new marker(ss.str(), "coord", coordinate_font, l.xPos * mc::MapX, 0, l.zPos * mc::MapZ);
        markers.push_back(m);
      }
    }
    
    if (s.show_warps) {
      text::font_face warp_font = font;
      
      if (s.has_warp_color) {
        warp_font.set_color(s.warp_color);
      }
      
      std::vector<warp>::iterator wit = wdb.warps.begin();
      
      /* initial code for projecting warps */
      for (; wit != wdb.warps.end(); wit++) { 
        warp w = *wit;
        
        if (w.zPos / mc::MapZ < s.min_z) continue;
        if (w.zPos / mc::MapZ > s.max_z) continue;
        if (w.xPos / mc::MapX < s.min_x) continue;
        if (w.xPos / mc::MapX > s.max_x) continue;
        
        marker *m = new marker(w.name, "warp", warp_font, w.xPos, w.yPos, w.zPos);
        markers.push_back(m);
      }
    }
  }
  
  if (s.write_markers) {
    write_markers(s, all, world, markers);
  }
  else {
    overlay_markers(s, all, world, markers);
  }
  
  if (!s.silent) cout << "Saving image..." << endl;
  
  if (s.binary) {
    progress_c = cout_progress_b_image;
  }
  
  if (s.use_pixelsplit) {
    std::map<point2, image_base*> parts = image_split(all, s.pixelsplit);
    //boost::ptr_map<point2, image_base> parts;
    
    for (std::map<point2, image_base*>::iterator it = parts.begin(); it != parts.end(); it++) {
      const point2 p = it->first;
      image_base* img = it->second;
      
      stringstream ss;
      ss << boost::format(output) % p.x % p.y;
      
      if (!img->save_png(ss.str(), "Map generated by c10t", progress_c)) {
        return false;
      }
    }
  }
  else {
    //image_base* img = new virtual_image(100, 100, all, 300, 300);
    image_base* img = all;
    
    if (!img->save_png(output, "Map generated by c10t", progress_c)) {
      error << strerror(errno);
      return false;
    }
  }
  
  delete all;
  return true;
}

bool do_world(settings_t& s, fs::path world_path, string output) {
  if (output.empty()) {
    error << "You must specify output file using '-o' to generate map";
    return false;
  }
  
  if (s.use_split || s.use_pixelsplit) {
    try {
      boost::format(output) % 0 % 0;
    } catch (boost::io::too_many_args& e) {
      error << "The `-o' parameter must contain two number format specifiers `%d' (x and y coordinates) - example: -o out/base.%d.%d.png";
      return false;
    }
  }
  
  if (!s.nocheck)
  {
    fs::path level_dat = world_path / "level.dat";
    
    if (!fs::exists(level_dat)) {
      error << "Does not exist: " << level_dat;
      return false;
    }

    fs::path output_path = fs::system_complete(fs::path(output));

    fs::path output_parent = output_path.parent_path();
    
    if (!fs::is_directory(output_parent)) {
      error << "Output directory does not exist: " << output_parent.string();
      return false;
    }
  }
  
  players_db pdb(s, world_path / "players");
  warps_db wdb(s);
  
  if (!s.silent) cout << "Working on " << s.threads << " thread(s)... " << endl;
  
  if (!s.silent) {
    cout << "world:  " << world_path << " " << endl;
    cout << "output: " << output << " " << endl;
    cout << endl;
  }
  
  void (*progress_c)(int part, int all) = NULL;

  if (s.binary) {
    progress_c = cout_progress_b_parse;
  }
  else if (!s.silent) {
    progress_c = cout_progress_ionly_n;
  }
  
  if (!s.silent) cout << "Performing broad phase scan of world directory... " << endl;
  world_info world(s, world_path, progress_c);
  if (!s.silent) cout << "found " << world.levels.size() << " files!" << endl;

  if (!s.use_split) {
    return do_one_world(s, world, pdb, wdb, output);
  }
  
  world_info** worlds = world.split(s.split);

  int i = 0;
  
  while (worlds[i] != NULL) {
    world_info* current = worlds[i++];
    
    stringstream ss;
    ss << boost::format(output) % current->chunk_x % current->chunk_y;
    
    if (!do_one_world(s, *current, pdb, wdb, ss.str())) {
      return false;
    }
  }
  
  delete [] worlds;
  return true;
}

int do_help() {
  cout << "This program was made possible because of the work and inspiration by ZomBuster and Firemark" << endl;
  cout << "" << endl;
  cout << "Written by Udoprog et al." << endl;
  cout << "" << endl;
  cout << "The following libraries are in use for this program:" << endl
       << "  zlib (compression)"                  << endl
       << "    http://www.zlib.net"               << endl
       << "  boost (thread, filesystem)"          << endl
       << "    http://www.boost.org"              << endl
       << "  libpng (portable network graphics)"  << endl
       << "    http://www.libpng.org"             << endl
       << "  libfreetype (font loading)"          << endl
       << "    http://www.freetype.org"           << endl
       << "" << endl;
# if defined(C10T_DISABLE_THREADS)
  cout << endl;
  cout << "C10T_DISABLE_THREADS: Threads has been disabled for this build" << endl;
# endif
  cout << endl;
  cout << "Usage: c10t [options]" << endl;
  cout << "Options:" << endl
       /*******************************************************************************/
    << "  -w, --world <world>       - use this world directory as input                " << endl
    << "  -o, --output <output>     - use this file as output file for generated png   " << endl
    << endl
    << "  -s, --silent              - execute silently, printing nothing except errors " << endl
    << "  -h, --help                - display this help text                           " << endl
    << "  -v, --version             - display version information                      " << endl
    << "  -D, --debug               - display debug information while executing        " << endl
    << "  -l, --list-colors         - list all available colors and block types        " << endl
    << endl
    << "  -t, --top <int>           - splice from the top, must be less than 128       " << endl
    << "  -b, --bottom <int>        - splice from the bottom, must be greater than or  " << endl
    << "                              equal to zero.                                   " << endl
    << "  -L, --limits <int-list>   - limit render to certain area. int-list form:     " << endl
    << "                              North,South,East,West, e.g.                      " << endl
    << "                              -L 0,100,-10,20 limiting between 0 and 100 in the" << endl
    << "                              north-south direction and between -10 and 20 in  " << endl
    << "                              the east-west direction.                         " << endl
    << "                              Note: South and West are the positive directions." << endl
    << endl
    << "Filtering options:" << endl
    << "  -e, --exclude <blockid>   - exclude block-id from render (multiple occurences" << endl
    << "                              is possible)                                     " << endl
    << "  -i, --include <blockid>   - include only this block-id in render (multiple   " << endl
    << "                              occurences is possible)                          " << endl
    << "  -a, --hide-all            - show no blocks except those specified with '-i'  " << endl
    << "  -c, --cave-mode           - cave mode - top down until solid block found,    " << endl
    << "                              then render bottom outlines only                 " << endl
    << "  -n, --night               - night-time rendering mode                        " << endl
    << "  -H, --heightmap           - heightmap rendering mode                         " << endl
    << endl
    << "  -N, --no-check            - ignore missing <world>/level.dat                 " << endl
    << endl
    << "Rendering options:" << endl
    << "  -q, --oblique             - oblique rendering                                " << endl
    << "  -y, --oblique-angle       - oblique angle rendering                          " << endl
    << "  -z, --isometric           - Isometric rendering                              " << endl
    << "  -r <degrees>              - rotate the rendering 90, 180 or 270 degrees CW   " << endl
    << endl
    << "  -m, --threads <int>       - Specify the amount of threads to use, for maximum" << endl
    << "                              efficency, this should match the amount of cores " << endl
    << "                              on your machine                                  " << endl
    << "  -B <set>                  - Specify the base color for a specific block id   " << endl
    << "                              <set> has the format <blockid>=<color>           " << endl
    << "                              <8 digit hex> specifies the RGBA values as       " << endl
    << "                              `<int>,<int>,<int>[,<int>]'. The side color will " << endl
    << "                              be a darkened variant of the base                " << endl
    << "                              example: `-B Grass=0,255,0,120'                  " << endl
    << "  -S <set>                  - Specify the side color for a specific block id   " << endl
    << "                              this uses the same format as '-B' only the color " << endl
    << "                              is applied to the side of the block              " << endl
    << "  -p, --split <chunks>      - Split the render into chunks, <output> must be a " << endl
    << "                              name containing two number format specifiers `%d'" << endl
    << "                              for `x' and `y' coordinates of the chunks        " << endl
    << endl
    << "Other Options:" << endl
    << "  -x, --binary              - Will output progress information in binary form, " << endl
    << "                              good for integration with third party tools      " << endl
    << "  --require-all             - Will force c10t to require all chunks or fail    " << endl
    << "                              not ignoring bad chunks                          " << endl
    << "  --show-players[=NICKLIST] - Will draw out player position and names from the " << endl
    << "                              players database in <world>/players              " << endl
    << "                              it is possible to define which nicks to show by  " << endl
    << "                              specifying a comma separated list of nicks       " << endl
    << "  --show-signs[=PREFIX]     - Will draw out signs from all chunks, if PREFIX   " << endl
    << "                              is specified, only signs matching the prefix will" << endl
    << "                              be drawn                                         " << endl
    << "  --show-warps=<file>       - Will draw out warp positions from the specified  " << endl
    << "                              warps.txt file, as used by hey0's mod            " << endl
    << "  --show-coordinates        - Will draw out each chunks expected coordinates   " << endl
    << "  -M, --memory-limit <MB>   - Will limit the memory usage caching operations to" << endl
    << "                              file when necessary                              " << endl
    << "  -C, --cache-file <file>   - Cache file to use when memory usage is reached   " << endl
    << "  -P <file>                 - use <file> as palette, each line should take the " << endl
    << "                              form: <block-id> ' ' <color> ' ' <color>         " << endl
    << "  -W <file>                 - write the default color palette to <file>, this  " << endl
    << "                              is useful for figuring out how to write your own " << endl
    << "  --pedantic-broad-phase    - Will enforce that all level chunks are parsable  " << endl
    << "                              during broad phase by getting x/y/z positions    " << endl
    << "                              from a quick parsing                             " << endl
    << "  --no-alpha                - Set all colors alpha channel to opaque (solid)   " << endl
    << "  --striped-terrain         - Darken every other block on a vertical basis     " << endl
    << "                              which helps to distinguish heights               " << endl
    << "  --write-markers <file>    - Write markers to <file> in JSON format instead of" << endl
    << "                              printing them on map                             " << endl
    << endl
    << "Font Options:" << endl
    << "  --ttf-path <font>         - Use the following ttf file when drawing text.    " << endl
    << "                              defaults to `font.ttf'                           " << endl
    << "  --ttf-size <size>         - Use the specified font size when drawing text.   " << endl
    << "                              defaults to `12'                                 " << endl
    << "  --ttf-color <color>       - Use the specified color when drawing text.       " << endl
    << "                              defaults to `0,0,0,255' (black)                  " << endl
    << "  --sign-color <color>      - Use the specified color when drawing signs.      " << endl
    << "                              defaults to <ttf-color>                          " << endl
    << "  --player-color <color>    - Use the specified color when showing players.    " << endl
    << "                              defaults to <ttf-color>                          " << endl
    << "  --warp-color <color>      - Use the specified color when showing warps.      " << endl
    << "                              defaults to <ttf-color>                          " << endl
    << "  --coordinate-color <color>                                                   " << endl
    << "                            - Use the specified color when drawing coordinates." << endl
    << "                              defaults to <ttf-color>                          " << endl
    << "  --cache-key <key>         - Indicates that c10t should cache operations using" << endl
    << "                              the unique cache key <key>, this should represent" << endl
    << "                              an unique combination of options. The cache files" << endl
    << "                              will be put in                                   " << endl
    << "                              <cache-dir>/<cache-key>/c.<coord>.cmap           " << endl
    << "  --cache-dir <dir>         - Use the following directory as cache directory   " << endl
    << "                              defaults to 'cache' if not specified             " << endl
    << "  --cache-compress          - Compress the cache files using zlib compression  " << endl
       /*******************************************************************************/
    << endl;
  cout << endl;
  cout << "Typical usage:" << endl;
  cout << "    c10t -w /path/to/world -o /path/to/png.png" << endl;
  cout << endl;
  cout << "  Utilize render cache and apply a 256 MB memory restriction (rest will be written to image.dat):" << endl;
  cout << "    c10t -w /path/to/world -o /path/to/png.png --cache-key='compressed' --cache-compress -M 256 -C image.dat" << endl;
  cout << endl;
  cout << "  Print out player positions using the font `example.ttf'" << endl;
  cout << "    c10t -w /path/to/world -o /path/to/png.png --show-players --ttf-font example.ttf" << endl;
  cout << endl;
  cout << "  Split the result into multiple files, using 10 chunks across in each file, the two number formatters will be replaced with the x/z positions of the chunks" << endl;
  cout << "    c10t -w /path/to/world -o /path/to/png.%d.%d.png --split 10" << endl;
  cout << endl;
  return 0;
}

int do_version() {
  cout << "c10t - a cartography tool for minecraft" << endl;
# if defined(C10T_DISABLE_THREADS)
  cout << endl;
  cout << "C10T_DISABLE_THREADS: Threads has been disabled for this build" << endl;
  cout << endl;
# endif
  cout << "version " << C10T_VERSION << endl;
  cout << "by: " << C10T_CONTACT << endl;
  cout << "site: " << C10T_SITE << endl;
  return 0;
}

int do_colors() {
  cout << "List of material Colors (total: " << mc::MaterialCount << ")" << endl;
  
  for (int i = 0; i < mc::MaterialCount; i++) {
    cout << i << ": " << mc::MaterialName[i] << " = " << mc::MaterialColor[i] << endl;
  }
  
  return 0;
}

bool get_blockid(const string blockid_string, int& blockid) {
  for (int i = 0; i < mc::MaterialCount; i++) {
    if (blockid_string.compare(mc::MaterialName[i]) == 0) {
      blockid = i;
      return true;
    }
  }
  
  try {
    blockid = boost::lexical_cast<int>(blockid_string);
  } catch(const boost::bad_lexical_cast& e) {
    error << "Cannot be converted to number: " << blockid_string;
    return false;
  }
  
  if (!(blockid >= 0 && blockid < mc::MaterialCount)) {
    error << "Not a valid blockid: " << blockid_string;
    return false;
  }
  
  return true;
}

bool parse_color(const string value, color& c) {
  int cr, cg, cb, ca=0xff;
  
  if (!(sscanf(value.c_str(), "%d,%d,%d,%d", &cr, &cg, &cb, &ca) == 4 || 
        sscanf(value.c_str(), "%d,%d,%d", &cr, &cg, &cb) == 3)) {
    error << "color sets must be of the form <red>,<green>,<blue>[,<alpha>] but was: " << value;
    return false;
  }
  
  if (!(
      cr >= 0 && cr <= 0xff &&
      cg >= 0 && cg <= 0xff &&
      cb >= 0 && cb <= 0xff &&
      ca >= 0 && ca <= 0xff)) {
    error << "color values must be between 0-255";
    return false;
  }
  
  c.r = cr;
  c.g = cg;
  c.b = cb;
  c.a = ca;
  return true;
}

bool parse_set(const char* set_str, int& blockid, color& c)
{
  istringstream iss(set_str);
  string key, value;
  
  assert(getline(iss, key, '='));
  assert(getline(iss, value));
  
  if (!get_blockid(key, blockid)) {
    return false;
  }

  if (!parse_color(value, c)) {
    return false;
  }
  
  return true;
}

bool do_base_color_set(const char *set_str) {
  int blockid;
  color c;
  
  if (!parse_set(set_str, blockid, c)) {
    return false;
  }

  mc::MaterialColor[blockid] = c;
  mc::MaterialSideColor[blockid] = mc::MaterialColor[blockid];
  mc::MaterialSideColor[blockid].darken(0x20);
  return true;
}

bool do_side_color_set(const char *set_str) {
  int blockid;
  color c;
  
  if (!parse_set(set_str, blockid, c)) {
    return false;
  }

  mc::MaterialSideColor[blockid] = color(c);
  return true;
}

// Convert a string such as "-30,40,50,30" to the corresponding N,S,E,W integers,
// and fill in the min/max settings.
bool parse_limits(const string& limits_str, settings_t& s) {
  std::vector<std::string> limits;
  boost::split(limits, limits_str, boost::is_any_of(","));
  
  if (limits.size() != 4) {
    error << "Limit argument must of format: <N>,<S>,<E>,<W>";
    return false;
  }
  
  s.min_x = atoi(limits[0].c_str());
  s.max_x = atoi(limits[1].c_str());
  s.min_z = atoi(limits[2].c_str());
  s.max_z = atoi(limits[3].c_str());
  return true;
}

bool parse_list(std::set<string>& set, const string s) {
  boost::char_separator<char> sep(" \t\n\r,:");
  typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
  tokenizer tokens(s, sep);
  
  for (tokenizer::iterator tok_iter = tokens.begin(); tok_iter != tokens.end(); ++tok_iter) {
    set.insert(*tok_iter);
  }

  if (set.size() == 0) {
    error << "List must specify items separated by comma `,'";
    return false;
  }
  
  return true;
}

bool do_write_palette(settings_t& s, string& path) {
  std::ofstream pal(path.c_str());

  pal << "#" << left << setw(20) << "<block-id>" << setw(16) << "<base R,G,B,A>" << " " << setw(16) << "<side R,G,B,A>" << '\n';
  
  for (int i = 0; i < mc::MaterialCount; i++) {
    color mc = mc::MaterialColor[i];
    color msc = mc::MaterialSideColor[i];
    pal << left << setw(20) << mc::MaterialName[i] << " " << setw(16) << mc << " " << setw(16) << msc << '\n';
  }

  if (pal.fail()) {
    error << "Failed to write palette to " << path;
    return false;
  }
  
  if (!s.silent) cout << "Sucessfully wrote palette to " << path << endl;
  
  return true;
}

bool do_read_palette(settings_t& s, string& path) {
  std::ifstream pal(path.c_str());
  boost::char_separator<char> sep(" \t\n\r");
  
  typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
  
  while (!pal.eof()) {
    string line;
    getline(pal, line, '\n');
    
    tokenizer tokens(line, sep);
    
    int blockid = 0, i = 0;
    color c;
    
    for (tokenizer::iterator tok_iter = tokens.begin(); tok_iter != tokens.end(); ++tok_iter, ++i) {
      string token = *tok_iter;
      
      if (token.at(0) == '#') {
        // rest is comment
        break;
      }
      
      switch(i) {
        case 0:
          if (!get_blockid(token, blockid)) {
            return false;
          }
          break;
        case 1:
          if (!parse_color(token, c)) {
            return false;
          }
          
          mc::MaterialColor[blockid] = c;
          c.darken(0x20);
          mc::MaterialSideColor[blockid] = c;
          break;
        case 2:
          if (!parse_color(token, c)) {
            return false;
          }
          
          mc::MaterialSideColor[blockid] = c;
          break;
        default:
          break;
      }
    }
  }
  
  if (!s.silent) cout << "Sucessfully read palette from " << path << endl;
  return true;
}

int main(int argc, char *argv[]){
  mc::initialize_constants();

  settings_t s;
  
  string world_path;
  string output_path("out.png");
  string palette_write_path, palette_read_path;
  
  int c, blockid;

  int option_index;

  int flag;

  struct option long_options[] =
   {
     {"world",            required_argument, 0, 'w'},
     {"output",           required_argument, 0, 'o'},
     {"top",              required_argument, 0, 't'},
     {"bottom",           required_argument, 0, 'b'},
     {"limits",           required_argument, 0, 'L'},
     {"memory-limit",     required_argument, 0, 'M'},
     {"cache-file",       required_argument, 0, 'C'},
     {"exclude",          required_argument, 0, 'e'},
     {"include",          required_argument, 0, 'i'},
     {"rotate",           required_argument, 0, 'r'},
     {"threads",          required_argument, 0, 'm'},
     {"split",            required_argument, 0, 'p'},
     {"help",             no_argument, 0, 'h'},
     {"silent",           no_argument, 0, 's'},
     {"version",          no_argument, 0, 'v'},
     {"debug",            no_argument, 0, 'D'},
     {"list-colors",      no_argument, 0, 'l'},
     {"hide-all",         no_argument, 0, 'a'},
     {"no-check",         no_argument, 0, 'N'},
     {"oblique",          no_argument, 0, 'q'},
     {"oblique-angle",    no_argument, 0, 'y'},
     {"isometric",        no_argument, 0, 'z'},
     {"cave-mode",        no_argument, 0, 'c'},
     {"night",            no_argument, 0, 'n'},
     {"heightmap",        no_argument, 0, 'H'},
     {"binary",           no_argument, 0, 'x'},
     {"require-all",      no_argument, &flag, 0},
     {"show-players",     optional_argument, &flag, 1},
     {"ttf-path",         required_argument, &flag, 2},
     {"ttf-size",         required_argument, &flag, 3},
     {"ttf-color",        required_argument, &flag, 4},
     {"show-coordinates",     no_argument, &flag, 5},
     {"pedantic-broad-phase", no_argument, &flag, 6},
     {"show-signs",       optional_argument, &flag, 7},
     {"sign-color",        required_argument, &flag, 8},
     {"player-color",        required_argument, &flag, 9},
     {"coordinate-color",        required_argument, &flag, 10},
     {"cache-key",       required_argument, &flag, 11},
     {"cache-dir",       required_argument, &flag, 12},
     {"cache-compress",       no_argument, &flag, 13},
     {"no-alpha",       no_argument, &flag, 14},
     {"striped-terrain",       no_argument, &flag, 15},
     {"write-markers",       required_argument, &flag, 16},
     {"pixelsplit",       required_argument, &flag, 17},
     {"show-warps",       required_argument, &flag, 18},
     {"warp-color",       required_argument, &flag, 19},
     {0, 0, 0, 0}
  };

  bool exclude_all = false;
  bool excludes[mc::MaterialCount];
  bool includes[mc::MaterialCount];
  
  for (int i = 0; i < mc::MaterialCount; i++) {
    excludes[i] = false;
    includes[i] = false;
  }
  
  while ((c = getopt_long(argc, argv, "DNvxcnHqzyalshM:C:L:w:o:e:t:b:i:m:r:W:P:B:S:p:", long_options, &option_index)) != -1)
  {
    blockid = -1;
    
    if (c == 0) {
      switch (flag) {
      case 0:
        s.require_all = true;
        break;
      case 1:
        s.show_players = true;
        if (optarg != NULL) {
          if (!parse_list(s.show_players_set, optarg)) {
            goto exit_error;
          }
        }
        break;
      case 2:
        s.ttf_path = optarg;
        break;
      case 3:
        s.ttf_size = atoi(optarg);
        
        if (s.ttf_size <= 0) {
          error << "ttf-size must be greater than 0";
          goto exit_error;
        }
        
        break;
      case 4:
        if (!parse_color(optarg, s.ttf_color)) {
          goto exit_error;
        }
        break;
      case 5:
        s.show_coordinates = true;
        break;
      case 6:
        s.pedantic_broad_phase = true;
        break;
      case 7:
        s.show_signs = true;

        if (optarg) {
          s.show_signs_filter = optarg;
          
          if (s.show_signs_filter.empty()) {
            error << "Sign filter must not be empty string";
            goto exit_error;
          }
        }
        
        break;
      case 8:
        if (!parse_color(optarg, s.sign_color)) {
          goto exit_error;
        }
        
        s.has_sign_color = true;
        break;
      case 9:
        if (!parse_color(optarg, s.player_color)) {
          goto exit_error;
        }
        
        s.has_player_color = true;
        break;
      case 10:
        if (!parse_color(optarg, s.coordinate_color)) {
          goto exit_error;
        }
        
        s.has_coordinate_color = true;
        break;
      case 11:
        s.cache_use = true;
        s.cache_key = optarg;
        break;
      case 12:
        s.cache_dir = optarg;
        break;
      case 13:
        s.cache_compress = true;
        break;
      case 14:
        for (int i = mc::Air + 1; i < mc::MaterialCount; i++)
          mc::MaterialColor[i].a = 0xff, mc::MaterialSideColor[i].a = 0xff;
        break;
      case 15: s.striped_terrain = true; break;
      case 16:
        s.write_markers = true;

        s.write_markers_path = fs::system_complete(fs::path(optarg));

        {
          fs::path parent = s.write_markers_path.parent_path();
          
          if (!fs::is_directory(parent)) {
            error << "Not a directory: " << parent.string();
            goto exit_error;
          }
        }
        
        break;
      case 17:
        if (s.use_split) {
          error << "Both `split' and `pixelsplit' cannot be used at the same time";
          goto exit_error;
        }
        
        try {
          s.pixelsplit = boost::lexical_cast<int>(optarg);
        } catch(boost::bad_lexical_cast& e) {
          error << "Cannot be converted to number: " << optarg;
          goto exit_error;
        }
        
        if (!(s.pixelsplit >= 1)) {
          error << "pixelsplit argument must be greater or equal to one";
          goto exit_error;
        }

        s.use_pixelsplit = true;
        
        break;
      case 18:
        s.show_warps = true;
        s.show_warps_path = fs::system_complete(fs::path(optarg));

        if (!fs::is_regular(s.show_warps_path)) {
          error << "Not a file: " << optarg;
          goto exit_error;
        }
        break;
      case 19:
        if (!parse_color(optarg, s.warp_color)) {
          goto exit_error;
        }
        
        s.has_warp_color = true;
        break;
      }
      
      continue;
    }
    
    switch (c)
    {
    case 'v':
      return do_version();
    case 'h':
      return do_help();
    case 'e':
      if (!get_blockid(optarg, blockid)) goto exit_error;
      excludes[blockid] = true;
      break;
    case 'm':
      s.threads = atoi(optarg);
      
      if (s.threads <= 0) {
        error << "Number of worker threads must be more than 0";
        goto exit_error;
      }
      
      break;
    case 'p':
      if (s.use_pixelsplit) {
        error << "Both `split' and `pixelsplit' cannot be used at the same time";
        goto exit_error;
      }
      
      try {
        s.split = boost::lexical_cast<int>(optarg);
      } catch(boost::bad_lexical_cast& e) {
        error << "Cannot be converted to number: " << optarg;
        goto exit_error;
      }
      
      if (!(s.split >= 1)) {
        error << "split argument must be greater or equal to one";
        goto exit_error;
      }
      
      s.use_split = true;
      break;
    case 'q':
      s.mode = Oblique;
      break;
    case 'z':
      s.mode = Isometric;
      break;
    case 'D':
      s.debug = true;
      break;
    case 'y':
      s.mode = ObliqueAngle;
      break;
    case 'a':
      exclude_all = true;
      break;
    case 'i':
      if (!get_blockid(optarg, blockid)) goto exit_error;
      includes[blockid] = true;
      break;
    case 'w': world_path = optarg; break;
    case 'o': output_path = optarg; break;
    case 's': s.silent = true; break;
    case 'x':
      s.silent = true;
      s.binary = true;
      break;
    case 'r':
      s.rotation = atoi(optarg) % 360;
      if (s.rotation < 0) {
        s.rotation += 360;
      }
      if (s.rotation % 90 != 0) {
        error << "Rotation must be a multiple of 90 degrees";
        goto exit_error;
      }

      break;
    case 'N': s.nocheck = true; break;
    case 'n': s.night = true; break;
    case 'H': s.heightmap = true; break;
    case 'c': s.cavemode = true; break;
    case 't':
      s.top = atoi(optarg);
      
      if (!(s.top > s.bottom && s.top < mc::MapY)) {
        error << "Top limit must be between `<bottom limit> - " << mc::MapY << "', not " << s.top;
        goto exit_error;
      }
      
      break;
    case 'L':
      if (!parse_limits(optarg, s)) {
        goto exit_error;
      }
      break;
    case 'b':
      s.bottom = atoi(optarg);
      
      if (!(s.bottom < s.top && s.bottom >= 0)) {
        error << "Bottom limit must be between `0 - <top limit>', not " << s.bottom;
        goto exit_error;
      }
      
      break;
    case 'l':
      return do_colors();
    case 'M':
      {
        int memory = atoi(optarg);
        assert(memory >= 0);
        s.memory_limit = memory * 1024 * 1024;
      }
      break;
    case 'C':
      s.cache_file = optarg;
      break;
    case 'W': palette_write_path = optarg; break;
    case 'P': palette_read_path = optarg; break;
    case 'B':
      if (!do_base_color_set(optarg)) goto exit_error;
      break;
    case 'S':
      if (!do_side_color_set(optarg)) goto exit_error;
      break;
    case '?':
      if (optopt == 'c')
        error << "Option -" << optopt << " requires an argument";
      else if (isprint (optopt))
        error << "Unknown option `-" << optopt << "'";
      else
        error << "Unknown option character `\\x" << std::hex << static_cast<int>(optopt) << "'.";

       goto exit_error;
    default:
      abort ();
    }
  }

  if (!s.cache_key.empty()) {
    if (!fs::is_directory(s.cache_dir)) {
      error << "Directory required for caching: " << s.cache_dir.string();
      goto exit_error;
    }

    s.cache_dir = s.cache_dir / s.cache_key;
  }

  if (exclude_all) {
    for (int i = 0; i < mc::MaterialCount; i++) {
      s.excludes[i] = true;
    }
  }

  for (int i = 0; i < mc::MaterialCount; i++) {
    if (excludes[i]) {
      s.excludes[i] = true;
    }
    
    if (includes[i]) {
      s.excludes[i] = false;
    }
  }
  
  if (output_path.compare("-") == 0) {
    s.silent = true;
  }
  
  if (!s.silent) {
    cout << "Type `-h' for help" << endl;
  }
  
  if (s.cache_use) {
    if (!fs::is_directory(s.cache_dir)) {
      if (!s.silent) cout << "Creating directory for caching: " << s.cache_dir.string() << endl;
      fs::create_directory(s.cache_dir);
    }
    
    if (s.cache_compress) {
      if (!s.silent) cout << "Cache compression is ON" << std::endl;
    }
    else {
      if (!s.silent) cout << "Cache compression is OFF" << std::endl;
    }
  }
  
  if (!palette_write_path.empty()) {
    if (!do_write_palette(s, palette_write_path)) {
      goto exit_error;
    }
  }
  
  if (!palette_read_path.empty()) {
    if (!do_read_palette(s, palette_read_path)) {
      goto exit_error;
    }
  }

  if (!world_path.empty()) {
    if (!do_world(s, fs::path(world_path), output_path))  {
      goto exit_error;
    }
  }
  
  if (s.binary) {
    cout_end();
  }
  else {
    if (!s.silent) cout << argv[0] << ": all done!" << endl;
  }
  
  mc::deinitialize_constants();
  return 0;

exit_error:
  if (s.binary) {
    cout_error(error.str());
  }
  else {
    if (!s.silent) cout << argv[0] << ": " << error.str() << endl;
  }

  mc::deinitialize_constants();
  return 1;
}
