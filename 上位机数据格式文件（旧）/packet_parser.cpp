#include "lgfT510.h"
#include <chrono>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace std;
using fcomplex = complex<float>;

constexpr int ch_N = 64;
constexpr int pa_N = 2 * ch_N;
constexpr int D_NUM = 1024 / ch_N;
constexpr uint DATA_LEN = 1024;

inline void convert_packet(const uint64_t *src, int32_t *dst, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    uint64_t val = src[i];
    uint32_t a = val & 0xFFFFFFFF;
    uint32_t b = (val >> 32) & 0xFFFFFFFF;
    dst[2 * i] = convert_uint32_to_int32(a);
    dst[2 * i + 1] = convert_uint32_to_int32(b);
  }
}

int main(int argc, char *argv[]) {
  ios::sync_with_stdio(false);
  cin.tie(nullptr);

  // ===================== 命令行参数解析 =====================
  if (argc != 3) {
    fprintf(stderr, "用法: %s <输入.bin文件> <输出文件前缀>\n", argv[0]);
    fprintf(stderr, "示例: %s s21_63tone.bin data/s21-sweep-data\n", argv[0]);
    return 1;
  }

  const char *input_file = argv[1];    // 输入 .bin
  const char *output_prefix = argv[2]; // 输出前缀

  // 构建输出文件名
  char complex_file[512];
  char id_file[512];
  snprintf(complex_file, sizeof(complex_file), "%s_complex.bin", output_prefix);
  snprintf(id_file, sizeof(id_file), "%s_id.bin", output_prefix);

  // ===================== 内存映射读取文件 =====================
  int fd = open(input_file, O_RDONLY);
  if (fd < 0) {
    perror("open 文件失败");
    return 1;
  }

  struct stat st{};
  fstat(fd, &st);
  size_t file_size = st.st_size;
  size_t u64_count = file_size / sizeof(uint64_t);

  void *map = mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (map == MAP_FAILED) {
    perror("mmap 失败");
    return 1;
  }

  const uint64_t *data = (const uint64_t *)map;
  printf("文件: %s\n", input_file);
  printf("总数据长度: %zu 个 uint64\n", u64_count);

  // ===================== 解析数据包 =====================
  printf("解数据包中...\n");
  size_t skip = 16 * ZHEN_LEN;
  auto packets = parse_stream(data + skip, u64_count - skip, DATA_LEN);
  size_t N = packets.size();
  printf("解数据包完成！包数量: %zu\n", N);

  // ===================== 输出数据预分配 =====================
  vector<fcomplex> ch_data(ch_N * N * D_NUM);
  vector<uint64_t> packets_id(N);
  fcomplex *ch_ptr = ch_data.data();
  uint64_t *id_ptr = packets_id.data();

  vector<int32_t> tmp_buf;
  tmp_buf.reserve(1024 * 16);

  // ===================== 处理数据 =====================
  printf("计算DDC输出数据中...\n");
  auto t1 = chrono::high_resolution_clock::now();

  for (size_t i = 0; i < N; ++i) {
    const auto &pkt = packets[i];
    id_ptr[i] = pkt.package_id;

    tmp_buf.resize(pkt.data_len * 2);
    convert_packet(pkt.data_ptr, tmp_buf.data(), pkt.data_len);
    const int32_t *tmp = tmp_buf.data();

    for (int j = 0; j < D_NUM; ++j) {
      size_t base = pa_N * j;
      for (int ch = 0; ch < ch_N; ++ch) {
        int32_t re = tmp[base + 2 * ch];
        int32_t im = tmp[base + 2 * ch + 1];
        size_t idx = ch * N * D_NUM + D_NUM * i + j;
        ch_ptr[idx] = fcomplex((float)re, (float)im);
      }
    }
  }

  auto t2 = chrono::high_resolution_clock::now();
  double ms = chrono::duration<double, milli>(t2 - t1).count();
  printf("处理完成！耗时: %.2f ms\n", ms);

  // ===================== 高速保存 =====================
  printf("保存数据:\n  -> %s\n  -> %s\n", complex_file, id_file);

  FILE *f1 = fopen(complex_file, "wb");
  fwrite(ch_data.data(), sizeof(fcomplex), ch_data.size(), f1);
  fclose(f1);

  FILE *f2 = fopen(id_file, "wb");
  fwrite(packets_id.data(), sizeof(uint64_t), packets_id.size(), f2);
  fclose(f2);

  // 清理
  munmap(map, file_size);
  close(fd);
  printf("全部完成！\n");
  return 0;
}
