//
//  shape.hpp
//  tensor-ugens
//
//  Tensor shapes: up to kMaxRank dimensions, stored row-major.
//

#ifndef shape_h
#define shape_h

const int kMaxRank = 4;

struct Shape {
    int dims[kMaxRank];
};

int Shape_size(Shape* s);
int Shape_strides(Shape* s, int strides[4]);
bool Shape_equal(const Shape* a, const Shape* b);

Shape Shape_scalar();
Shape Shape_row(int n);
Shape Shape_col(int n);
Shape Shape_plane(int n);
Shape Shape_matrix(int rows, int cols);
Shape Shape_cuboid(int planes, int rows, int cols);

// Each dimension is the least common multiple of a's and b's.
Shape Shape_broadcast(Shape* a, Shape* b);

#endif /* shape_h */
