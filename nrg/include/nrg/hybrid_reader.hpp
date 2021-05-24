// hybrid_reader.hpp

#include <nrg/reader.hpp>

#include <vector>

namespace nrgprf
{

    class hybrid_reader : public reader
    {
    private:
        std::vector<const reader*> _readers;

        template<typename... Ts>
        using all_reader_ptrs = typename std::enable_if<
            std::conjunction<std::is_convertible<Ts*, reader*>...>::value, bool>::type;

    public:
        template<typename... Readers, all_reader_ptrs<Readers...> = true>
        hybrid_reader(const Readers&... reader) :
            _readers({ &reader... })
        {}

        void push_back(const reader& r);

        error read(sample&) const override;
        error read(sample&, uint8_t ev_idx) const override;
        size_t num_events() const override;
    };

}
