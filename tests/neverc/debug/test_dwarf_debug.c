// RUN: %neverc -g -c %s -o %t.o
typedef struct Point {
    int x;
    int y;
} Point;

typedef struct Rect {
    Point origin;
    int width;
    int height;
} Rect;

typedef enum Color {
    RED   = 0,
    GREEN = 1,
    BLUE  = 2
} Color;

typedef struct Shape {
    Rect bounds;
    Color color;
    unsigned flags : 4;
    unsigned visible : 1;
} Shape;

static int point_distance_sq(Point a, Point b) {
    int dx = a.x - b.x;
    int dy = a.y - b.y;
    return dx * dx + dy * dy;
}

int compute_area(const Rect *r) {
    int w = r->width;
    int h = r->height;
    return w * h;
}

int shape_score(Shape *shapes, int count) {
    int total = 0;
    for (int i = 0; i < count; i++) {
        Shape *s = &shapes[i];
        if (!s->visible) continue;
        int area = compute_area(&s->bounds);
        int color_weight = (s->color == RED) ? 3 : 1;
        total += area * color_weight;
    }
    return total;
}

int main(int argc, char **argv) {
    Shape shapes[3] = {
        { .bounds = { {0, 0}, 10, 20 }, .color = RED,   .flags = 0xA, .visible = 1 },
        { .bounds = { {5, 5}, 30, 40 }, .color = GREEN, .flags = 0x3, .visible = 1 },
        { .bounds = { {1, 1}, 50, 60 }, .color = BLUE,  .flags = 0xF, .visible = 0 },
    };

    int score = shape_score(shapes, 3);
    int dist = point_distance_sq(shapes[0].bounds.origin, shapes[1].bounds.origin);

    int result = 0;
    if (score != 10 * 20 * 3 + 30 * 40 * 1) result = 1;
    if (dist != 25 + 25) result = 1;

    for (int i = 0; i < argc && i < 3; i++) {
        result += shapes[i].flags;
    }

    return result;
}
