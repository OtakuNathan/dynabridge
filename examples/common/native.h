#ifndef PORTAL_EXAMPLES_COMMON_NATIVE_H
#define PORTAL_EXAMPLES_COMMON_NATIVE_H

namespace portal {
    namespace example_native {
        inline int add(int left, unsigned right) {
            return left + static_cast<int>(right);
        }

        class counter {
        public:
            explicit counter(unsigned value)
                : value_(static_cast<int>(value)) {
            }

            int add(int value) const {
                return value_ + value;
            }

            int value() const {
                return value_;
            }

        private:
            int value_;
        };
    }
}

#endif
