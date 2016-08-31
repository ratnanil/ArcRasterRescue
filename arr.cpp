#include <zlib.h>
#include <type_traits>
#include <cstring>
#include <sstream>
#include <string>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <vector>
#include <bitset>
#include <limits>
#include <cassert>
#include "arr.hpp"

#include <cstdio>
#include <jpeglib.h>

void bitsetToString(const std::vector< uint8_t > &bs){
  for(const auto &v: bs){
    std::bitset<8> t(v);
    std::cerr<<t<<" ";
  }
  std::cerr<<std::endl;
}

std::string hexify(int raster_num){
  std::stringstream ss;
  ss << "a" << std::setfill('0') << std::setw(8) << std::hex << raster_num << ".gdbtable";
  return ss.str();
}


template<class T>
T ReadThing(std::ifstream &fin){
  T v;
  fin.read( reinterpret_cast <char*> (&v), sizeof( T ) );
  return v;
}

uint8_t ReadByte(std::ifstream &fin){
  return ReadThing<uint8_t>(fin);
}

std::vector<uint8_t> ReadBytes(std::ifstream &fin, int count){
  std::vector<uint8_t> ret(count);
  fin.read( reinterpret_cast <char*>(ret.data()), sizeof( uint8_t )*count );
  return ret;
}

std::string ReadBytesAsString(std::ifstream &fin, int count){
  auto v = ReadBytes(fin,count);
  return std::string(v.begin(),v.end());
}

int16_t ReadInt16(std::ifstream &fin){
  #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return ReadThing<int16_t>(fin);
  #else
    #pragma message "Big-endian unimplemented"
  #endif
}

int32_t ReadInt32(std::ifstream &fin){
  #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return ReadThing<int32_t>(fin);
  #else
    #pragma message "Big-endian unimplemented"
  #endif
}

float ReadFloat32(std::ifstream &fin){
  #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return ReadThing<float>(fin);
  #else
    #pragma message "Big-endian unimplemented"
  #endif
}

double ReadFloat64(std::ifstream &fin){
  #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return ReadThing<double>(fin);
  #else
    #pragma message "Big-endian unimplemented"
  #endif
}

uint64_t ReadVarUint(std::ifstream &fin){
  uint64_t shift = 0;
  uint64_t ret   = 0;
  while(true){
    uint8_t b = ReadByte(fin);
    ret      |= ((b & 0x7F) << shift);
    if( (b & 0x80)==0)
      break;
    shift += 7;
  }
  return ret;
}

void AdvanceBytes(std::ifstream &fin, int64_t count){
  fin.seekg(count, std::ios_base::cur);
}

void GotoPosition(std::ifstream &fin, int64_t pos){
  fin.seekg(pos);
}



void Field::print() const {
  std::cout<<"Name     = "<<name      <<"\n"
           <<"Alias    = "<<alias     <<"\n"
           <<"Type     = "<<(int)type <<"\n"
           <<"Nullable = "<<nullable  <<"\n";
}



std::string GetString(std::ifstream &fin, int nbcar=-1){
  std::string temp;

  if(nbcar==-1)
    nbcar = ReadByte(fin);

  for(int j=0;j<nbcar;j++){
    temp += ReadByte(fin);
    AdvanceBytes(fin,1);
  }
  return temp;
}

int32_t GetCount(std::ifstream &fin){
  int32_t count = ReadByte(fin);
  count += ((int)ReadByte(fin))*256;
  return count;
}



void Zinflate(std::vector<uint8_t> &src, std::vector<uint8_t> &dst) {
  z_stream strm  = {0};
  strm.total_in  = strm.avail_in  = src.size();
  strm.total_out = strm.avail_out = dst.size();
  strm.next_in   = (Bytef *) src.data();
  strm.next_out  = (Bytef *) dst.data();

  strm.zalloc = Z_NULL;
  strm.zfree  = Z_NULL;
  strm.opaque = Z_NULL;

  int err = -1;
  int ret = -1;

  err = inflateInit2(&strm, (15 + 32)); //15 window bits, and the +32 tells zlib to to detect if using gzip or zlib
  if(err!=Z_OK){
    inflateEnd(&strm);
    throw std::runtime_error("zlib error: "+std::to_string(err));
  }

  err = inflate(&strm, Z_FINISH);
  if (err!=Z_STREAM_END) {
   inflateEnd(&strm);
   throw std::runtime_error("zlib error: "+std::to_string(err));
  }

  ret = strm.total_out;
  inflateEnd(&strm);
  dst.resize(ret);
}

//Based on "memdjpeg" by Kenneth Finnegan (2012, blog.thelifeofkenneth.com)
void JPEGinflate(std::vector<uint8_t> &src, std::vector<uint8_t> &dst){
  assert(src.size()>0);
  
  uint8_t      prevbyte = src[0];
  unsigned int jpegstart;

  for(jpegstart=1;jpegstart<src.size();jpegstart++)
    if(prevbyte==0xFF && src[jpegstart]==0xD8)
      break;
    else
      prevbyte = src[jpegstart];

  jpegstart -= 1;

  #ifdef EXPLORE
    std::cerr<<"JPEG BLOB header: ";
    for(unsigned int i=0;i<jpegstart;i++)
      std::cerr<<std::hex<<(int)src[i]<<std::dec<<" ";
    std::cerr<<std::endl;
  #endif

  src.erase(src.begin(),src.begin()+jpegstart);

  //SETUP   

  // Variables for the decompressor itself
  struct jpeg_decompress_struct cinfo;
  struct jpeg_error_mgr jerr;

  //START

  // Allocate a new decompress struct, with the default error handler.
  // The default error handler will exit() on pretty much any issue,
  // so it's likely you'll want to replace it or supplement it with
  // your own.
  cinfo.err = jpeg_std_error(&jerr);  
  jpeg_create_decompress(&cinfo);


  // Configure this decompressor to read its data from a memory 
  // buffer starting at unsigned char *jpg_buffer, which is jpg_size
  // long, and which must contain a complete jpg already.
  //
  // If you need something fancier than this, you must write your 
  // own data source manager, which shouldn't be too hard if you know
  // what it is you need it to do. See jpeg-8d/jdatasrc.c for the 
  // implementation of the standard jpeg_mem_src and jpeg_stdio_src 
  // managers as examples to work from.
  jpeg_mem_src(&cinfo, src.data(), src.size());

  // Have the decompressor scan the jpeg header. This won't populate
  // the cinfo struct output fields, but will indicate if the
  // jpeg is valid.
  int rc = jpeg_read_header(&cinfo, TRUE);
  if (rc != 1)
    throw std::runtime_error("File does not seem to be a normal JPEG");

  // By calling jpeg_start_decompress, you populate cinfo
  // and can then allocate your output bitmap buffers for
  // each scanline.
  jpeg_start_decompress(&cinfo);
  
  int width      = cinfo.output_width;
  int height     = cinfo.output_height;
  int pixel_size = cinfo.output_components;

  std::cerr<<"width: "<<width<<std::endl;
  std::cerr<<"height: "<<height<<std::endl;
  std::cerr<<"pixel_size: "<<pixel_size<<std::endl;

  uint64_t bmp_size = width * height * pixel_size;
  dst.resize(bmp_size);

  // The row_stride is the total number of bytes it takes to store an
  // entire scanline (row). 
  const int row_stride = width * pixel_size;


  // Now that you have the decompressor entirely configured, it's time
  // to read out all of the scanlines of the jpeg.
  //
  // By default, scanlines will come out in RGBRGBRGB...  order, 
  // but this can be changed by setting cinfo.out_color_space
  //
  // jpeg_read_scanlines takes an array of buffers, one for each scanline.
  // Even if you give it a complete set of buffers for the whole image,
  // it will only ever decompress a few lines at a time. For best 
  // performance, you should pass it an array with cinfo.rec_outbuf_height
  // scanline buffers. rec_outbuf_height is typically 1, 2, or 4, and 
  // at the default high quality decompression setting is always 1.
  while (cinfo.output_scanline < cinfo.output_height) {
    uint8_t *buffer_array[1];
    buffer_array[0] = dst.data() + cinfo.output_scanline * row_stride;

    jpeg_read_scanlines(&cinfo, buffer_array, 1);
  }


  // Once done reading *all* scanlines, release all internal buffers,
  // etc by calling jpeg_finish_decompress. This lets you go back and
  // reuse the same cinfo object with the same settings, if you
  // want to decompress several jpegs in a row.
  //
  // If you didn't read all the scanlines, but want to stop early,
  // you instead need to call jpeg_abort_decompress(&cinfo)
  jpeg_finish_decompress(&cinfo);

  // At this point, optionally go back and either load a new jpg into
  // the jpg_buffer, or define a new jpeg_mem_src, and then start 
  // another decompress operation.
  
  // Once you're really really done, destroy the object to free everything
  jpeg_destroy_decompress(&cinfo);

  //DONE
  
  // Write the decompressed bitmap out to a ppm file, just to make sure 
  // it worked. 
  // fd = open("output.ppm", O_CREAT | O_WRONLY, 0666);
  // char buf[1024];

  // rc = sprintf(buf, "P6 %d %d 255\n", width, height);
  // write(fd, buf, rc); // Write the PPM image header before data
  // write(fd, bmp_buffer, bmp_size); // Write out all RGB pixel data

  // close(fd);
  // free(bmp_buffer);
}



//TODO: The following assumes that data is always stored in big endian order.
//This should be confirmed.
template<class T>
std::vector<T> Unpack(std::vector<uint8_t> &packed, const int block_width, const int block_height){
  std::vector<T> output(block_width*block_height);
  packed.resize(sizeof(T)*block_width*block_height);

  if(std::is_same<T,float>::value){
    #if __FLOAT_WORD_ORDER__ == __ORDER_LITTLE_ENDIAN__
      for(unsigned int i=0;i<packed.size();i+=4){
        std::swap(packed[i+0],packed[i+3]);
        std::swap(packed[i+1],packed[i+2]);
      }
    #endif
  } else if(std::is_same<T,double>::value){
    #if __FLOAT_WORD_ORDER__ == __ORDER_LITTLE_ENDIAN__
      for(unsigned int i=0;i<packed.size();i+=8){
        std::swap(packed[i+0],packed[i+7]);
        std::swap(packed[i+1],packed[i+6]);
        std::swap(packed[i+2],packed[i+5]);
        std::swap(packed[i+3],packed[i+4]);
      }
    #endif
  } else if(std::is_same<T,int16_t>::value){
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
      for(unsigned int i=0;i<packed.size();i+=2)
        std::swap(packed[i+0],packed[i+1]);
    #endif
  } else if(std::is_same<T,int32_t>::value){
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
      for(unsigned int i=0;i<packed.size();i+=4){
        std::swap(packed[i+0],packed[i+3]);
        std::swap(packed[i+1],packed[i+2]);
      }
    #endif
  } else if(std::is_same<T,uint16_t>::value){
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
      for(unsigned int i=0;i<packed.size();i+=2)
        std::swap(packed[i+0],packed[i+1]);
    #endif
  } else if(std::is_same<T,uint32_t>::value){
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
      for(unsigned int i=0;i<packed.size();i+=4){
        std::swap(packed[i+0],packed[i+3]);
        std::swap(packed[i+1],packed[i+2]);
      }
    #endif
  } else if(std::is_same<T, uint8_t>::value || std::is_same<T, int8_t>::value){
    //No special unpacking needs to be done for these
  } else {
    std::cerr<<"Unimplemented type conversion for '"<<typeid(T).name()<<"'! (Use c++filt -t to decode.)"<<std::endl;
    throw std::runtime_error("Unimplemented type conversion!");
  }

  memcpy(output.data(), packed.data(), sizeof(T)*block_width*block_height);

  return output;
}




std::string BaseTable::getFilenameX(std::string filename){
  return filename.substr(0,filename.size()-1)+"x";
}

void BaseTable::getFlags(){
  if(has_flags){
    auto nremainingflags = nullable_fields;
    while(nremainingflags>0){
      auto tempflag = ReadByte(gdbtable);
      flags.push_back(tempflag);
      nremainingflags -= 8;
    }
  }
}

bool BaseTable::skipField(const Field &field, uint8_t &ifield_for_flag_test){
  if(has_flags && field.nullable){
    uint8_t test = (flags[ifield_for_flag_test >> 3] & (1 << (ifield_for_flag_test % 8)));
    ifield_for_flag_test++;
    return test!=0;
  }
  return false;
}

BaseTable::BaseTable(std::string filename){
  std::string filenamex = getFilenameX(filename);
  gdbtablx.open(filenamex, std::ios_base::in | std::ios_base::binary);

  #ifdef EXPLORE
    std::cerr<<"Opening BaseTable as '"<<filename<<"'..."<<std::endl;
  #endif

  if(!gdbtablx.good()){
    std::cerr<<"Could not find '"<<filenamex<<"'!"<<std::endl;
    throw std::runtime_error("Could not find '"+filenamex+"'!");
  }

  gdbtablx.seekg(8);
  nfeaturesx         = ReadInt32(gdbtablx);
  size_tablx_offsets = ReadInt32(gdbtablx);

  gdbtable.open(filename, std::ios_base::in | std::ios_base::binary);

  if(!gdbtable.good()){
    std::cerr<<"Could not find '"<<filename<<"'!"<<std::endl;
    throw std::runtime_error("Could not find '"+filename+"'!");
  }

  gdbtable.seekg(4);
  auto nfeatures = ReadInt32(gdbtable);

  gdbtable.seekg(32);
  auto header_offset = ReadInt32(gdbtable);

  gdbtable.seekg(header_offset);
  auto header_length = ReadInt32(gdbtable);

  AdvanceBytes(gdbtable,4);

  //1 = point
  //2 = multipoint
  //3 = polyline
  //4 = polygon
  //9 = multipatch
  auto layer_geom_type = ReadByte(gdbtable);

  AdvanceBytes(gdbtable,3);

  auto nfields = GetCount(gdbtable);

  // std::cout<<"nfeaturesx         = "<<nfeaturesx           <<"\n";
  // std::cout<<"size_tablx_offsets = "<<size_tablx_offsets   <<"\n";
  // std::cout<<"nfeatures          = "<<nfeatures            <<"\n";
  // std::cout<<"header_offset      = "<<header_offset        <<"\n";
  // std::cout<<"header_length      = "<<header_length        <<"\n";
  // std::cout<<"layer_geom_type    = "<<(int)layer_geom_type <<"\n";
  // std::cout<<"nfields            = "<<nfields              <<"\n";

  has_flags       = false;
  nullable_fields = 0;

  for(int i=0;i<nfields;i++){
    int8_t nbcar;

    auto field = Field();

    field.name     = GetString(gdbtable);
    field.alias    = GetString(gdbtable);
    field.type     = ReadByte(gdbtable);
    field.nullable = true;
    //print('type = %d (%s)' % (type, field_type_to_str(type))) //TODO

    if(field.type==6){        //ObjectID
      auto magic_byte1 = ReadByte(gdbtable);
      auto magic_byte2 = ReadByte(gdbtable);
      field.nullable   = false;
    } else if(field.type==7){ //Shape
      auto magic_byte1 = ReadByte(gdbtable); //0
      auto flag =  ReadByte(gdbtable);       //6 or 7
      if( (flag & 1)==0 )
        field.nullable = false;


      auto wkt_len = GetCount(gdbtable);
      field.shape.wkt = GetString(gdbtable, wkt_len/2);

      auto magic_byte3 = ReadByte(gdbtable);

      field.shape.has_m = false;
      field.shape.has_z = false;
      if(magic_byte3==5)
        field.shape.has_z = true;
      if(magic_byte3==7){
        field.shape.has_m = true;
        field.shape.has_z = true;
      }

      field.shape.xorig   = ReadFloat64(gdbtable);
      field.shape.yorig   = ReadFloat64(gdbtable);
      field.shape.xyscale = ReadFloat64(gdbtable);
      if(field.shape.has_m){
        field.shape.morig = ReadFloat64(gdbtable);
        field.shape.mscale = ReadFloat64(gdbtable);
      }
      if(field.shape.has_z){
        field.shape.zorig = ReadFloat64(gdbtable);
        field.shape.zscale = ReadFloat64(gdbtable);
      }
      field.shape.xytolerance = ReadFloat64(gdbtable);
      if(field.shape.has_m)
        field.shape.mtolerance = ReadFloat64(gdbtable);
      if(field.shape.has_z)
        field.shape.ztolerance = ReadFloat64(gdbtable);

      field.shape.xmin = ReadFloat64(gdbtable);
      field.shape.ymin = ReadFloat64(gdbtable);
      field.shape.xmax = ReadFloat64(gdbtable);
      field.shape.ymax = ReadFloat64(gdbtable);

      while(true){
        auto read5 = ReadBytes(gdbtable,5);
        if(read5[0]!=0 || (read5[1]!=1 && read5[1]!=2 && read5[1]!=3) || read5[2]!=0 || read5[3]!=0 || read5[4]!=0){
          AdvanceBytes(gdbtable,-5);
          auto datum = ReadFloat64(gdbtable);
        } else {
          for(int i=0;i<read5[1];i++)
            auto datum = ReadFloat64(gdbtable);
          break;
        }
      }

    } else if(field.type==4){ //String
      auto width = ReadInt32(gdbtable);
      auto flag = ReadByte(gdbtable);
      if( (flag&1)==0 )
        field.nullable = false;
      auto default_value_length = ReadVarUint(gdbtable);
      if( (flag&4)!=0 && default_value_length>0){
        //auto default_value = ;
        AdvanceBytes(gdbtable, default_value_length);
      }

    } else if(field.type==8){
      AdvanceBytes(gdbtable,1);
      auto flag = ReadByte(gdbtable);
      if( (flag&1)==0 )
        field.nullable = false;

    } else if(field.type==9) { //Raster
      AdvanceBytes(gdbtable,1);
      auto flag = ReadByte(gdbtable);
      if( (flag & 1)==0 )
        field.nullable = false;

      field.raster.raster_column = GetString(gdbtable);

      auto wkt_len     = GetCount(gdbtable);
      field.raster.wkt = GetString(gdbtable,wkt_len/2);

      #ifdef EXPLORE
        std::cerr<<"WKT: "<<field.raster.wkt<<std::endl;
      #endif

      //f.read(82) //TODO: Was like this in source.

      auto magic_byte3 = ReadByte(gdbtable);

      if(magic_byte3>0){
        field.raster.raster_has_m = false;
        field.raster.raster_has_z = false;
        if(magic_byte3==5)
          field.raster.raster_has_z = true;
        if(magic_byte3==7){
          field.raster.raster_has_m = true;
          field.raster.raster_has_z = true;
        }

        field.raster.raster_xorig   = ReadFloat64(gdbtable);
        field.raster.raster_yorig   = ReadFloat64(gdbtable);
        field.raster.raster_xyscale = ReadFloat64(gdbtable);

        if(field.raster.raster_has_m){
          field.raster.raster_morig  = ReadFloat64(gdbtable);
          field.raster.raster_mscale = ReadFloat64(gdbtable);
        }

        if(field.raster.raster_has_z){
          field.raster.raster_zorig  = ReadFloat64(gdbtable);
          field.raster.raster_zscale = ReadFloat64(gdbtable);
        }

        field.raster.raster_xytolerance  = ReadFloat64(gdbtable);
        if(field.raster.raster_has_m)
          field.raster.raster_mtolerance = ReadFloat64(gdbtable);
        if(field.raster.raster_has_z)
          field.raster.raster_ztolerance = ReadFloat64(gdbtable);
      }

      AdvanceBytes(gdbtable,1);

    } else if(field.type==11 || field.type==10 || field.type==12){ //UUID or XML
      auto width         = ReadByte(gdbtable);
      auto flag          = ReadByte(gdbtable);
      if( (flag&1)==0 )
        field.nullable = false;
    } else {
      auto width    = ReadByte(gdbtable);
      auto flag     = ReadByte(gdbtable);
      if( (flag&1)==0 )
        field.nullable = false;

      auto default_value_length = ReadByte(gdbtable);

      if( (flag&4)!=0 ){
        if(field.type==0 && default_value_length==2)
          auto default_value = ReadInt16(gdbtable);
        else if(field.type==1 && default_value_length==4)
          auto default_value = ReadInt32(gdbtable);
        else if(field.type==2 && default_value_length==4)
          auto default_value = ReadFloat32(gdbtable);
        else if(field.type==3 && default_value_length==8)
          auto default_value = ReadFloat64(gdbtable);
        else if(field.type==5 && default_value_length==8)
          auto default_value = ReadFloat64(gdbtable);
        else
          AdvanceBytes(gdbtable, default_value_length);
      }
    }

    if(field.nullable){
      has_flags        = true;
      nullable_fields += 1;
    }

    if(field.type!=6)
      fields.push_back(field);

    //std::cout<<"\n\nField Number = "<<(fields.size()-1)<<"\n";
    //field.print();
  }


}


MasterTable::MasterTable(std::string filename) : BaseTable(filename) {
  #ifdef EXPLORE
    std::cerr<<"gdbtables found:\n";
  #endif
  for(int f=0;f<nfeaturesx;f++){
    GotoPosition(gdbtablx, 16 + f * size_tablx_offsets);
    auto feature_offset = ReadInt32(gdbtablx);

    if(feature_offset==0)
      continue;

    GotoPosition(gdbtable, feature_offset);

    auto blob_len = ReadInt32(gdbtable);

    getFlags();

    uint8_t ifield_for_flag_test = 0;
    for(unsigned int fi=0;fi<fields.size();fi++){
      if(skipField(fields[fi],ifield_for_flag_test))
        continue;

      if(fields[fi].type==1){
        auto val = ReadInt32(gdbtable);
      } else if(fields[fi].type == 10 || fields[fi].type == 11){ //10=DatasetGUID
        auto val = ReadBytes(gdbtable, 16);
      } else if(fields[fi].type == 4 || fields[fi].type == 12){  //String
        auto length = ReadVarUint(gdbtable);
        auto val    = ReadBytesAsString(gdbtable, length);
        auto loc    = val.find("fras_ras_");
        #ifdef EXPLORE
          std::cerr<<"\t"<<val<<" - "<<hexify(f+1)<<"\n";
        #endif
        if(loc!=std::string::npos)
          rasters.emplace_back(val.substr(9), f);
      }
    }
  }
}





/* Plus 0
FID = 1
feature_offset = 498
blob_len = 96
flags = [0, 0, 252]
Type: 1
Field  sequence_nbr : 1
Type: 1
Field     raster_id : 1
Type: 4
Field          name : ""
Type: 1
Field    band_flags : 2048
Type: 1
Field    band_width : 1
Type: 1
Field   band_height : 1
Type: 1
Field    band_types : 4195328
Type: 1
Field   block_width : 128
Type: 1
Field  block_height : 128
Type: 3
Field block_origin_x : -178.500000
Type: 3
Field block_origin_y : 88.500000
Type: 3
Field         eminx : -180.000000
Type: 3
Field         eminy : 87.000000
Type: 3
Field         emaxx : -177.000000
Type: 3
Field         emaxy : 90.000000
Type: 1
Field         cdate : 1471710590
Type: 1
Field         mdate : 1471710590
Type: 1
Field          srid : 0
set([1, 3, 4, 6])
*/
RasterBase::RasterBase(std::string filename) : BaseTable(filename) {
  #ifdef EXPLORE
    std::cerr<<"Opening RasterBase as '"<<filename<<"'"<<std::endl;
  #endif

  for(int f=0;f<nfeaturesx;f++){
    GotoPosition(gdbtablx, 16 + f * size_tablx_offsets);
    auto feature_offset = ReadInt32(gdbtablx);

    if(feature_offset==0)
      continue;

    GotoPosition(gdbtable, feature_offset);

    auto blob_len = ReadInt32(gdbtable);

    getFlags();

    uint8_t ifield_for_flag_test = 0;
    for(unsigned int fi=0;fi<fields.size();fi++){
      if(skipField(fields[fi], ifield_for_flag_test))
        continue;

      //std::cerr<<"Field name: "<<fields[fi].name<<std::endl;

      if(fields[fi].type==1){
        if(fields[fi].name=="band_types"){
          //auto val = ReadInt32(gdbtable);
          //std::cerr<<"band_types = "<<val<<std::endl;
          band_types = ReadBytes(gdbtable, 4);
          #ifdef EXPLORE
            std::cerr<<"band_types = ";
            for(auto v: band_types)
              std::cerr<<std::hex<<(int)v<<" "<<std::dec;
            bitsetToString(band_types);

            std::cerr<<"Detected band data type = "<<data_type<<std::endl;
          #endif
          data_type        = bandTypeToDataTypeString(band_types);
          compression_type = bandTypeToCompressionTypeString(band_types);
        } else if(fields[fi].name=="block_width")
          block_width = ReadInt32(gdbtable);
        else if(fields[fi].name=="block_height")
          block_height = ReadInt32(gdbtable);
        else if(fields[fi].name=="band_width"){
          band_width = ReadInt32(gdbtable);
        } else if(fields[fi].name=="band_height"){
          band_height = ReadInt32(gdbtable);
        } else
          AdvanceBytes(gdbtable, 4);

      } else if(fields[fi].type == 4 || fields[fi].type == 12){
        auto length = ReadVarUint(gdbtable);
        auto val    = ReadBytes(gdbtable, length);
      } else if(fields[fi].type==3){
        auto val = ReadFloat64(gdbtable);
        if(fields[fi].name=="block_origin_x")
          block_origin_x=val;
        else if(fields[fi].name=="block_origin_y")
          block_origin_y=val;
        else if(fields[fi].name=="eminx")
          eminx=val;
        else if(fields[fi].name=="emaxx")
          emaxx=val;
        else if(fields[fi].name=="eminy")
          eminy=val;
        else if(fields[fi].name=="emaxy")
          emaxy=val;
      }
    }
  }

  //TODO: This is not guaranteed to be correct
  geotransform[0] = eminx;
  geotransform[1] = std::round((emaxx-eminx)/band_width);
  geotransform[2] = 0;
  geotransform[3] = emaxy;
  geotransform[4] = 0;
  geotransform[5] = -std::round((emaxy-eminy)/band_height); //Arc really doesn't seem to like rasters with this value positive.
}

/*
1_bit           0 4 8  0 00000000 00000100 00001000 00000000 
4_bit           0 4 20 0 00000000 00000100 00100000 00000000 
8_bit_signed    0 4 41 0 00000000 00000100 01000001 00000000 
8_bit_unsigned  0 4 40 0 00000000 00000100 01000000 00000000
16_bit_signed   0 4 81 0 00000000 00000100 10000001 00000000 
16_bit_unsigned 0 4 80 0 00000000 00000100 10000000 00000000 
32_bit_signed   0 4 1  1 00000000 00000100 00000001 00000001 
32_bit_float    0 4 2  1 00000000 00000100 00000010 00000001 
32_bit_unsigned 0 4 0  1 00000000 00000100 00000000 00000001 
64_bit          0 4 0  2 00000000 00000100 00000000 00000010 
*/
std::string RasterBase::bandTypeToDataTypeString(std::vector<uint8_t> &band_types) const {
  if(band_types[2]==0x08 && band_types[3]==0x00) //00000000 00000100 00001000 00000000
    return "1bit";
  if(band_types[2]==0x20 && band_types[3]==0x00) //00000000 00000100 00100000 00000000
    return "4bit";
  if(band_types[2]==0x41 && band_types[3]==0x00) //00000000 00000100 01000001 00000000
    return "int8_t";
  if(band_types[2]==0x40 && band_types[3]==0x00) //00000000 00000100 01000000 0000000
    return "uint8_t";
  if(band_types[2]==0x81 && band_types[3]==0x00) //00000000 00000100 10000001 00000000
    return "int16_t";
  if(band_types[2]==0x80 && band_types[3]==0x00) //00000000 00000100 10000000 00000000
    return "uint16_t";
  if(band_types[2]==0x01 && band_types[3]==0x01) //00000000 00000100 00000001 00000001
    return "int32_t";
  if(band_types[2]==0x02 && band_types[3]==0x01) //00000000 00000100 00000010 00000001
    return "float32";
  if(band_types[2]==0x00 && band_types[3]==0x01) //00000000 00000100 00000000 00000001
    return "uint32_t";
  if(band_types[2]==0x00 && band_types[3]==0x02) //00000000 00000100 00000000 00000010
    return "64bit";

  std::cerr<<"Unrecognised band data type!"<<std::endl;
  throw std::runtime_error("Unrecognised band data type!");
}


/*
uncompressed, float
band_types = 0 0 2  1 00000000 00000000 00000010 00000001 

LZ77, float32
band_types = 0 4 2  1 00000000 00000100 00000010 00000001 

JPEG, 75% quality, uint8_t
band_types = 0 8 40 0 00000000 00001000 01000000 00000000 

JPEG, 23% quality, uint8_t
band_types = 0 8 40 0 00000000 00001000 01000000 00000000 

JPEG2000 75% quality, int16_t
band_types = 0 c 81 0 00000000 00001100 10000001 00000000 

JPEG2000 23% quality, int16_t
band_types = 0 c 81 0 00000000 00001100 10000001 00000000 
*/
std::string RasterBase::bandTypeToCompressionTypeString(std::vector<uint8_t> &band_types) const {
  if(band_types[1]==0x00) //band_types = 0 0 2  1 00000000 00000000 00000010 00000001 
    return "uncompressed";
  if(band_types[1]==0x04) //band_types = 0 4 2  1 00000000 00000100 00000010 00000001 
    return "lz77";
  if(band_types[1]==0x08) //band_types = 0 8 40 0 00000000 00001000 01000000 00000000 
    return "jpeg";
  if(band_types[1]==0x0C) //band_types = 0 c 81 0 00000000 00001100 10000001 00000000 
    return "jpeg2000";

  std::cerr<<"Unrecognised band compression type!"<<std::endl;
  throw std::runtime_error("Unrecognised band compression type!");
}



/* Plus 1
#######################
###FILENAME: ../dump_gdbtable/001.gdb/a0000000e.gdbtable
nfeaturesx = 1
size_tablx_offsets = 5
nfeatures = 1
header_offset = 40
header_length = 2056
layer_geom_type = 4
polygon
nfields = 5

field = 0
nbcar = 3
name = OID
nbcar_alias = 0
alias = 
type = 6 (objectid)
magic1 = 4
magic2 = 2
nullable = 0 

field = 1
nbcar = 6
name = RASTER
nbcar_alias = 0
alias = 
type = 9 (raster)
flag = 5
nbcar = 13
raster_column = Raster Column
wkt = PROJCS["NAD83_UTM_zone_15N",GEOGCS["GCS_North_American_1983",DATUM["D_North_American_1983",SPHEROID["GRS_1980",6378137.0,298.257222101]],PRIMEM["Greenwich",0.0],UNIT["Degree",0.0174532925199433]],PROJECTION["Transverse_Mercator"],PARAMETER["false_easting",500000.0],PARAMETER["false_northing",0.0],PARAMETER["central_meridian",-93.0],PARAMETER["scale_factor",0.9996],PARAMETER["latitude_of_origin",0.0],UNIT["Meter",1.0]]
magic3 = 7
xorigin = -5120900.000000000000000
yorigin = -9998100.000000000000000
xyscale = 450445547.391053795814514
morigin = -100000.000000000000000
mscale = 10000.000000000000000
zorigin = -100000.000000000000000
zscale = 10000.000000000000000
xytolerance = 0.001000000000000
mtolerance = 0.001000000000000
ztolerance = 0.001000000000000
1
nullable = 1 

field = 2
nbcar = 9
name = FOOTPRINT
nbcar_alias = 0
alias = 
type = 7 (geometry)
magic1 = 0
flag = 7
wkt = PROJCS["NAD83_UTM_zone_15N",GEOGCS["GCS_North_American_1983",DATUM["D_North_American_1983",SPHEROID["GRS_1980",6378137.0,298.257222101]],PRIMEM["Greenwich",0.0],UNIT["Degree",0.0174532925199433]],PROJECTION["Transverse_Mercator"],PARAMETER["false_easting",500000.0],PARAMETER["false_northing",0.0],PARAMETER["central_meridian",-93.0],PARAMETER["scale_factor",0.9996],PARAMETER["latitude_of_origin",0.0],UNIT["Meter",1.0]]
magic3 = 7
xorigin = -5120900.000000000000000
yorigin = -9998100.000000000000000
xyscale = 450445547.391053795814514
morigin = -100000.000000000000000
mscale = 10000.000000000000000
zorigin = -100000.000000000000000
zscale = 10000.000000000000000
xytolerance = 0.001000000000000
mtolerance = 0.001000000000000
ztolerance = 0.001000000000000
xmin = 421568.000000000000000
ymin = 4872699.000000001862645
xmax = 428822.000000000000000
ymax = 4877607.000000003725290
cur_pos = 2015
0.0
nullable = 1 

field = 3
nbcar = 16
name = FOOTPRINT_Length
nbcar_alias = 0
alias = 
type = 3 (float64)
width = 8
flag = 3
default_value_length = 0
nullable = 1 

field = 4
nbcar = 14
name = FOOTPRINT_Area
nbcar_alias = 0
alias = 
type = 3 (float64)
width = 8
flag = 3
default_value_length = 0
nullable = 1 

------ROWS (FEATURES)------

FID = 1
feature_offset = 2100
blob_len = 101
flags = [240]
Type: 9
Field        RASTER : "" (len=1)
Type: 7
geom_len = 79
geom_type = 5 --> 5
polygon
nb_total_points: 5
nb_geoms: 1
minx = 421568.000000000000000
miny = 4872699.000000001862645
maxx = 428822.000000000640284
maxy = 4877607.000000003725290
nb_points[0] = 5
[1] 421568.000000000000000 4872699.000000001862645
[2] 421568.000000000000000 4877607.000000003725290
[3] 428822.000000000000000 4877607.000000003725290
[4] 428822.000000000000000 4872699.000000001862645
[5] 421568.000000000000000 4872699.000000001862645

cur_offset = 2189
Type: 3
Field FOOTPRINT_Length : 24324.000000
Type: 3
Field FOOTPRINT_Area : 35602632.000014
set([9, 3, 6, 7])
*/
RasterProjection::RasterProjection(std::string filename) : BaseTable(filename){
  #ifdef EXPLORE
    std::cerr<<"Opening RasterProjection as '"<<filename<<"'"<<std::endl;
  #endif
}


template<class T>
RasterData<T>::RasterData(std::string filename, const RasterBase &rb) : BaseTable(filename){
  //Determine maximum and minimum pixel coordinates from the data itself, since 
  //extracting them from the metadata is not yet reliable.
  getDimensionsFromData(filename,rb);

  //NOTE: In theory, the dimensions are given by
  //    resize(rb.band_width, rb.band_height, -9999);
  //However, the pixels themselves seem to have a non-obvious coordinate scheme 
  //which often takes them outside of this area. Therefore, we use
  //getDimensionsFromData() to determine the range
  resize(maxpx-minpx, maxpy-minpy, -9999);

  //NOTE: Calculating pixel coordinates as, e.g., `col_nbr*block_width+x` may
  //yield pixel values that are not in the range [0,band_width). The following
  //offsets seemed close to working on my test data:
  //  const int xoffset = -std::abs(rb.eminx-rb.block_origin_x);
  //  const int yoffset = -std::abs(rb.emaxy-rb.block_origin_y);
  //However, there were still pixels out of range even with this offset applied
  //and the offset calculation seems sufficiently ridiculous that I don't trust
  //it. Therefore, I have set the offsets to values generated by
  //getDimensionsFromData()
  const int xoffset = -minpx;
  const int yoffset = -minpy;

  #ifdef EXPLORE
    std::cerr<<"Opening Raster Data as "<<filename<<std::endl;
  #endif

  int skipped_points = 0;

  for(int f=0;f<nfeaturesx;f++){
    GotoPosition(gdbtablx, 16 + f * size_tablx_offsets);
    auto feature_offset = ReadInt32(gdbtablx);

    if(feature_offset==0)
      continue;

    GotoPosition(gdbtable, feature_offset);

    auto blob_len = ReadInt32(gdbtable);

    getFlags();

    int row_nbr    = -1;
    int col_nbr    = -1;
    int rrd_factor = -1;

    //std::cerr<<"Fields.size = "<<fields.size()<<std::endl;

    uint8_t ifield_for_flag_test = 0;
    for(unsigned int fi=0;fi<fields.size();fi++){
      if(skipField(fields[fi], ifield_for_flag_test))
        continue;

      if(fields[fi].type==1){
        auto val = ReadInt32(gdbtable);
        //std::cout<<fields[fi].name<<" = "<<val<<std::endl;
        if(fields[fi].name=="col_nbr")
          col_nbr = val;
        else if(fields[fi].name=="row_nbr")
          row_nbr = val;
        else if(fields[fi].name=="rrd_factor")
          rrd_factor = val;
      } else if(fields[fi].type == 4 || fields[fi].type == 12){
        auto length = ReadVarUint(gdbtable);
        auto val    = ReadBytes(gdbtable, length);
      } else if(fields[fi].type==8){ //Appears to be where raster data is stored
        auto length = ReadVarUint(gdbtable);

        //Skip that which is not a base layer
        if(rrd_factor!=0){
          AdvanceBytes(gdbtable, length);
          continue;
        }

        auto val = ReadBytes(gdbtable, length);

        if(length==0)
          continue;

        #ifdef EXPLORE
          std::cerr<<"row_nbr="<<row_nbr<<" col_nbr="<<col_nbr<<std::endl;
        #endif

        #ifdef EXPLORE
          std::cerr<<"Length = "<<val.size()<<std::endl;

          std::cerr<<"Compressed: ";
          for(uint8_t i=0;i<10;i++)
            std::cerr<<(int)val[i]<<" ";
          std::cerr<<std::endl;
        #endif

        std::vector<T> unpacked;

        //These two magic bytes indicate zlib compression... unless they don't,
        //since it is possible, if unlikely, for uncompressed data to begin a
        //block with these values. The `band_types` field has bits which
        //indicate compression, but appear to do so non-uniquely (lz77, lzw,
        //maybe others map to the same compression bits). Therefore, since the
        //compression indicators have not yet been entirely figured out, this
        //if- clause checks for the magic bytes and checks the length of the
        //field to determine probabilistically if compression is being used. The
        //check should be fairly robust, though, since we expect some degree of
        //compression for any non-pathological data.
        if(rb.compression_type=="lz77"){ 
          #ifdef EXPLORE
            std::cerr<<"Decompressing with zlib"<<std::endl;
          #endif
          std::vector<uint8_t> decompressed(1000000);
          Zinflate(val, decompressed);
          decompressed.resize(sizeof(T)*rb.block_width*rb.block_height); //Drop trailer
          unpacked = Unpack<T>(decompressed, rb.block_width, rb.block_height);

          #ifdef EXPLORE
            std::cout<<"Decompressed: ";
            for(unsigned int i=0;i<10;i++)
              std::cout<<std::hex<<(int)decompressed[i]<<std::dec<<" ";
            std::cout<<"\n";
          #endif
        } else if(rb.compression_type=="jpeg"){
          #ifdef EXPLORE
            std::cerr<<"Decompressing with JPEG"<<std::endl;
          #endif

          std::cerr<<"Warning: JPEG decompression is not fully functional yet, though it appears to be."<<std::endl;

          std::vector<uint8_t> decompressed;
          JPEGinflate(val, decompressed);

          #ifdef EXPLORE
            std::cerr<<"Decompressed size: "<<decompressed.size()<<std::endl;
          #endif

          unpacked.resize(decompressed.size());
          std::copy(decompressed.begin(), decompressed.end(), unpacked.begin());

          //unpacked = Unpack<T>(decompressed, rb.block_width, rb.block_height);

        } else if(rb.compression_type=="uncompressed") {
          #ifdef EXPLORE
            std::cerr<<"Assuming uncompressed data"<<std::endl;
          #endif
          unpacked = Unpack<T>(val, rb.block_width, rb.block_height);
        } else {
          std::cerr<<"Unimplemented compression type!"<<std::endl;
          #ifdef EXPLORE
            continue;
          #else
            throw std::runtime_error("Unimplemented compression type!");
          #endif
        }

        #ifdef EXPLORE
          std::cout<<"Unpacked: ";
          assert(unpacked.size()>=10);
          for(unsigned int i=0;i<10;i++)
            std::cout<<unpacked[i]<<" ";
          std::cout<<"\n";
        #endif

        //Save data to the numpy array
        for(int y=0;y<rb.block_height;y++)
        for(int x=0;x<rb.block_width;x++){
          int px = col_nbr*(rb.block_width )+x+xoffset;
          int py = row_nbr*(rb.block_height)+y+yoffset;
          if(in_raster(px,py)){
            operator()(px, py) = unpacked[y*(rb.block_width)+x];
          } else {
            skipped_points++;
          }
        }

      } else {
        std::cerr<<"Unrecognised field type: "<<(int)fields[fi].type<<std::endl;
      }
    }
  }

  #ifdef EXPLORE
    std::cerr<<"Skipped points: "<<std::dec<<skipped_points<<std::endl;
  #endif

  //TODO: FGDB doesn't seem to store NoData values for the data I tested in any
  //of the gdbtables. I also found it difficult to set NoData values. At least
  //for the floating-point data I was looking at, FGDB seems to use 0xff7fffff
  //as a filler value for blocks which are not entirely filled with data. This,
  //in addition to the dataset's intrinsic NoData value need to be marked as
  //such. In the following, I assume that 0xff7fffff, 0x00, and -9999 are all
  //NoData values and translate everything to -9999 for output.

  //TODO: 0xff7fffff is how the filler value was stored in memory for the FGDB I
  //was looking at. I don't know if this is consistent across different
  //architectures or not. The following worked on my machine, but may not work
  //on yours. Some careful thinking is needed to improve this section.
  const uint32_t arc_no_data      = 0xff7fffff; 
  const T *translated_arc_no_data = reinterpret_cast<const T*>(&arc_no_data);

  no_data = -9999; //TODO: This cannot always be NoData.

  for(uint64_t i=0;i<geodata.size();i++)
    if(geodata[i]==*translated_arc_no_data)
      geodata[i] = no_data;
    else if(geodata[i]==0) //TODO: Surely this is not always NoData?
      geodata[i] = no_data;
}


template<class T>
void RasterData<T>::getDimensionsFromData(std::string filename, const RasterBase &rb){
  minpx = std::numeric_limits<int>::max();
  minpy = std::numeric_limits<int>::max();
  maxpx = std::numeric_limits<int>::min();
  maxpy = std::numeric_limits<int>::min();

  for(int f=0;f<nfeaturesx;f++){
    GotoPosition(gdbtablx, 16 + f * size_tablx_offsets);
    auto feature_offset = ReadInt32(gdbtablx);

    if(feature_offset==0)
      continue;

    GotoPosition(gdbtable, feature_offset);

    auto blob_len = ReadInt32(gdbtable);

    getFlags();

    int row_nbr    = -1;
    int col_nbr    = -1;
    int rrd_factor = -1;

    uint8_t ifield_for_flag_test = 0;
    for(unsigned int fi=0;fi<fields.size();fi++){
      if(skipField(fields[fi], ifield_for_flag_test))
        continue;

      if(fields[fi].type==1){
        auto val = ReadInt32(gdbtable);
        if(fields[fi].name=="col_nbr")
          col_nbr = val;
        else if(fields[fi].name=="row_nbr")
          row_nbr = val;
        else if(fields[fi].name=="rrd_factor")
          rrd_factor = val;
      } else if(fields[fi].type == 4 || fields[fi].type == 12){
        auto length = ReadVarUint(gdbtable);
        auto val    = ReadBytes(gdbtable, length);
      } else if(fields[fi].type==8){ //Appears to be where raster data is stored
        if(rrd_factor!=0)
          continue;

        int px = col_nbr*rb.block_width;
        int py = row_nbr*rb.block_height;

        minpx = std::min(minpx,px);
        minpy = std::min(minpy,py);
        maxpx = std::max(maxpx,px+rb.block_width);
        maxpy = std::max(maxpy,py+rb.block_height);

        break;

      } else {
        std::cerr<<"Unrecognised field type: "<<(int)fields[fi].type<<std::endl;
      }
    }
  }
}


template<class T>
void RasterData<T>::resize(int64_t width, int64_t height, T no_data_val){
  this->width  = width;
  this->height = height;
  std::cerr<<"Allocating "<<sizeof(T)<<"x"<<width<<"x"<<height<<" = "<<(sizeof(T)*width*height)<<std::endl;
  geodata.resize(width*height);
  std::fill(geodata.begin(), geodata.end(), no_data_val);
}

template<class T>
bool RasterData<T>::in_raster(int x, int y) const {
  //std::cerr<<std::dec<<x<<" "<<width<<" "<<y<<" "<<height<<std::endl;
  return 0<=x && x<width && 0<=y && y<height;
}

template<class T>
T& RasterData<T>::operator()(int64_t x, int64_t y){
  return geodata[y*width+x];
}

template<class T>
T RasterData<T>::operator()(int64_t x, int64_t y) const {
  return geodata[y*width+x];
}

template<class T>
void RasterData<T>::setAll(T val){
  std::fill(geodata.begin(),geodata.end(),val);
}




template<class T>
void ExportTypedRasterToGeoTIFF(std::string operation, std::string basename, int raster_num, std::string outputname){
  BaseTable        bt(basename+hexify(raster_num));
  RasterBase       rb(basename+hexify(raster_num+4)); //Get the fras_bnd file
  RasterProjection rp(basename+hexify(raster_num+1));
  RasterData<T>    rd(basename+hexify(raster_num+3), rb);

  // for(int y=0;y<300;y++){
  //   for(int x=0;x<300;x++)
  //     std::cerr<<rd.geodata[y*rd.width+x]<<" ";
  //   std::cerr<<"\n";
  // }

  for(const auto &f: bt.fields)
    if(f.type==9){
      if(rd.projection!="")
        std::cerr<<"Ambiguity in which WKT projection to use. Using the last one."<<std::endl;
      rd.projection = f.raster.wkt;
    }

  #ifdef EXPLORE
    std::cerr<<"Projection: "<<rd.projection<<std::endl;
  #endif

  rd.geotransform = rb.geotransform;

  rd.save(outputname.c_str(),operation,false);
}

void ExportRasterToGeoTIFF(std::string operation, std::string basename, int raster_num, std::string outputname){
  RasterBase rb(basename+hexify(raster_num+4)); //Get the fras_bnd file

  if     (rb.data_type=="64bit")
    ExportTypedRasterToGeoTIFF<double>(operation, basename, raster_num, outputname);
  else if(rb.data_type=="float32")
    ExportTypedRasterToGeoTIFF<float>(operation, basename, raster_num, outputname);
  else if(rb.data_type=="uint8_t")
    ExportTypedRasterToGeoTIFF<uint8_t>(operation, basename, raster_num, outputname);
  else if(rb.data_type=="int16_t")
    ExportTypedRasterToGeoTIFF<int16_t>(operation, basename, raster_num, outputname);
  else if(rb.data_type=="int32_t")
    ExportTypedRasterToGeoTIFF<int32_t>(operation, basename, raster_num, outputname);
  else if(rb.data_type=="int8_t")
    ExportTypedRasterToGeoTIFF<int8_t>(operation, basename, raster_num, outputname);
  else if(rb.data_type=="uint16_t")
    ExportTypedRasterToGeoTIFF<uint16_t>(operation, basename, raster_num, outputname);
  else if(rb.data_type=="uint32_t")
    ExportTypedRasterToGeoTIFF<uint32_t>(operation, basename, raster_num, outputname);
  else if(rb.data_type=="4bit")
    ExportTypedRasterToGeoTIFF<uint8_t>(operation, basename, raster_num, outputname);
  else if(rb.data_type=="1bit")
    ExportTypedRasterToGeoTIFF<uint8_t>(operation, basename, raster_num, outputname);
  else
    std::cerr<<"Unrecognised raster data type: "<<rb.data_type<<"!"<<std::endl;
}
