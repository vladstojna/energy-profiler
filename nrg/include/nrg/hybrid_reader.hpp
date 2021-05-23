// hybrid_reader.hpp

#include <nrg/reader.hpp>

#include <vector>

namespace nrgprf
{

    class hybrid_reader : public reader
    {
    private:
        std::vector<const reader*> _readers;

    public:
        template<typename... Readers>
        hybrid_reader(const Readers*... args) :
            _readers({ args... })
        {}

        hybrid_reader(std::initializer_list<const reader*> lst) :
            _readers(lst)
        {}

        void push_back(const reader* r);

        error read(sample&) const override;
        error read(sample&, uint8_t ev_idx) const override;
        size_t num_events() const override;
    };

}
