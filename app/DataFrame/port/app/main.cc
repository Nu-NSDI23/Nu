#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <tuple>

#include <nu/runtime.hpp>
#include <cereal/types/string.hpp>

#include <DataFrame/DataFrame.h>

using namespace hmdf;

// Download dataset at https://www1.nyc.gov/site/tlc/about/tlc-trip-record-data.page.
// The following code is implemented based on the format of 2016 datasets.

static double haversine(double lat1, double lon1, double lat2, double lon2)
{
    // Distance between latitudes and longitudes
    double dLat = (lat2 - lat1) * M_PI / 180.0;
    double dLon = (lon2 - lon1) * M_PI / 180.0;

    // Convert to radians.
    lat1 = lat1 * M_PI / 180.0;
    lat2 = lat2 * M_PI / 180.0;

    // Apply formulae.
    double a   = pow(sin(dLat / 2), 2) + pow(sin(dLon / 2), 2) * cos(lat1) * cos(lat2);
    double rad = 6371;
    double c   = 2 * asin(sqrt(a));
    return rad * c;
}

StdDataFrame<uint64_t> load_data()
{
    return read_csv<-1, int, SimpleTime, SimpleTime, int, double, double, double, int, char, double,
                    double, int, double, double, double, double, double, double, double>(
        "/mnt/all.csv", "VendorID", "tpep_pickup_datetime", "tpep_dropoff_datetime",
        "passenger_count", "trip_distance", "pickup_longitude", "pickup_latitude", "RatecodeID",
        "store_and_fwd_flag", "dropoff_longitude", "dropoff_latitude", "payment_type",
        "fare_amount", "extra", "mta_tax", "tip_amount", "tolls_amount", "improvement_surcharge",
        "total_amount");
}

void print_number_vendor_ids_and_unique(StdDataFrame<uint64_t>& df)
{
    std::cout << "print_number_vendor_ids_and_unique()" << std::endl;
    std::cout << "Number of vendor_ids in the train dataset: "
              << df.get_column<int>("VendorID").size() << std::endl;
    std::cout << "Number of unique vendor_ids in the train dataset:"
              << df.get_col_unique_values<int>("VendorID").size() << std::endl;
    std::cout << std::endl;
}

void print_passage_counts_by_vendor_id(StdDataFrame<uint64_t>& df, int vendor_id)
{
    std::cout << "print_passage_counts_by_vendor_id(vendor_id), vendor_id = " << vendor_id
              << std::endl;

    auto sel_vendor_functor = [&](const uint64_t&, const int& vid) -> bool {
        return vid == vendor_id;
    };
    auto sel_df =
        df.get_data_by_sel<int, decltype(sel_vendor_functor), int, SimpleTime, double, char>(
            "VendorID", sel_vendor_functor);
    auto& passage_count_vec = sel_df.get_column<int>("passenger_count");
    auto sealed_passage_count_vec = nu::to_sealed_ds(std::move(passage_count_vec));
    std::map<int, int> passage_count_map;
    for (auto passage_count : sealed_passage_count_vec) {
        passage_count_map[passage_count]++;
    }
    passage_count_vec = nu::to_unsealed_ds(std::move(sealed_passage_count_vec));
    for (auto& [passage_count, cnt] : passage_count_map) {
        std::cout << "passage_count= " << passage_count << ", cnt = " << cnt << std::endl;
    }
    std::cout << std::endl;
}

void calculate_trip_duration(StdDataFrame<uint64_t>& df)
{
    std::cout << "calculate_trip_duration()" << std::endl;

    auto& pickup_time_vec  = df.get_column<SimpleTime>("tpep_pickup_datetime");
    auto sealed_pickup_time_vec = nu::to_sealed_ds(std::move(pickup_time_vec));
    auto& dropoff_time_vec = df.get_column<SimpleTime>("tpep_dropoff_datetime");
    auto sealed_dropoff_time_vec = nu::to_sealed_ds(std::move(dropoff_time_vec));
    assert(sealed_pickup_time_vec.size() == sealed_dropoff_time_vec.size());

    auto duration_vec = nu_make_sharded_vector<uint64_t>(sealed_pickup_time_vec.size());
    auto pickup_iter = sealed_pickup_time_vec.cbegin();
    auto dropoff_iter = sealed_dropoff_time_vec.cbegin();
    for (; pickup_iter != sealed_pickup_time_vec.cend(); ++pickup_iter, ++dropoff_iter) {
        auto pickup_time_second  = pickup_iter->to_second();
        auto dropoff_time_second = dropoff_iter->to_second();
        duration_vec.push_back(dropoff_time_second - pickup_time_second);
    }
    pickup_time_vec = nu::to_unsealed_ds(std::move(sealed_pickup_time_vec));
    dropoff_time_vec = nu::to_unsealed_ds(std::move(sealed_dropoff_time_vec));
    df.load_column("duration", std::move(duration_vec), nan_policy::dont_pad_with_nans);
    MaxVisitor<uint64_t> max_visitor;
    MinVisitor<uint64_t> min_visitor;
    MeanVisitor<uint64_t> mean_visitor;
    df.multi_visit(std::make_pair("duration", &max_visitor),
                   std::make_pair("duration", &min_visitor),
                   std::make_pair("duration", &mean_visitor));
    std::cout << "Mean duration = " << mean_visitor.get_result() << " seconds" << std::endl;
    std::cout << "Min duration = " << min_visitor.get_result() << " seconds" << std::endl;
    std::cout << "Max duration = " << max_visitor.get_result() << " seconds" << std::endl;
    std::cout << std::endl;
}

void calculate_distribution_store_and_fwd_flag(StdDataFrame<uint64_t>& df)
{
    std::cout << "calculate_distribution_store_and_fwd_flag()" << std::endl;

    auto sel_N_saff_functor = [&](const uint64_t&, const char& saff) -> bool {
        return saff == 'N';
    };
    auto N_df =
        df.get_data_by_sel<char, decltype(sel_N_saff_functor), int, SimpleTime, double, char>(
            "store_and_fwd_flag", sel_N_saff_functor);
    std::cout << static_cast<double>(N_df.get_index().size()) / df.get_index().size() << std::endl;

    auto sel_Y_saff_functor = [&](const uint64_t&, const char& saff) -> bool {
        return saff == 'Y';
    };
    auto Y_df =
        df.get_data_by_sel<char, decltype(sel_Y_saff_functor), int, SimpleTime, double, char>(
            "store_and_fwd_flag", sel_Y_saff_functor);
    auto sealed_unique_vendor_id_vec = nu::to_sealed_ds(Y_df.get_col_unique_values<int>("VendorID"));
    std::cout << '{';
    for (auto& vector_id : sealed_unique_vendor_id_vec) {
        std::cout << vector_id << ", ";
    }
    std::cout << '}' << std::endl;

    std::cout << std::endl;
}

void calculate_haversine_distance_column(StdDataFrame<uint64_t>& df)
{
    std::cout << "calculate_haversine_distance_column()" << std::endl;

    auto& pickup_longitude_vec        = df.get_column<double>("pickup_longitude");
    auto sealed_pickup_longitude_vec  = nu::to_sealed_ds(std::move(pickup_longitude_vec));
    auto& pickup_latitude_vec         = df.get_column<double>("pickup_latitude");
    auto sealed_pickup_latitude_vec   = nu::to_sealed_ds(std::move(pickup_latitude_vec));
    auto& dropoff_longitude_vec       = df.get_column<double>("dropoff_longitude");
    auto sealed_dropoff_longitude_vec = nu::to_sealed_ds(std::move(dropoff_longitude_vec));
    auto& dropoff_latitude_vec        = df.get_column<double>("dropoff_latitude");
    auto sealed_dropoff_latitude_vec  = nu::to_sealed_ds(std::move(dropoff_latitude_vec));
    assert(sealed_pickup_longitude_vec.size() == sealed_pickup_latitude_vec.size());
    assert(sealed_pickup_longitude_vec.size() == sealed_dropoff_longitude_vec.size());
    assert(sealed_pickup_longitude_vec.size() == sealed_dropoff_latitude_vec.size());
    auto haversine_distance_vec =
        nu_make_sharded_vector<double>(sealed_pickup_longitude_vec.size());
    auto pickup_longitude_iter  = sealed_pickup_longitude_vec.cbegin();
    auto pickup_latitude_iter = sealed_pickup_latitude_vec.cbegin();
    auto dropoff_longitude_iter = sealed_dropoff_longitude_vec.cbegin();
    auto dropoff_latitude_iter = sealed_dropoff_latitude_vec.cbegin();
    for (; pickup_longitude_iter != sealed_pickup_longitude_vec.cend();
         ++pickup_longitude_iter, ++pickup_latitude_iter, ++dropoff_longitude_iter,
         ++dropoff_latitude_iter) {
        haversine_distance_vec.emplace_back(haversine(*pickup_latitude_iter, *pickup_longitude_iter,
                                                      *dropoff_latitude_iter,
                                                      *dropoff_longitude_iter));
    }
    pickup_longitude_vec = nu::to_unsealed_ds(std::move(sealed_pickup_longitude_vec));
    pickup_latitude_vec = nu::to_unsealed_ds(std::move(sealed_pickup_latitude_vec));
    dropoff_longitude_vec = nu::to_unsealed_ds(std::move(sealed_dropoff_longitude_vec));
    dropoff_latitude_vec = nu::to_unsealed_ds(std::move(sealed_dropoff_latitude_vec));
    df.load_column("haversine_distance", std::move(haversine_distance_vec),
                   nan_policy::dont_pad_with_nans);
    auto sel_functor = [&](const uint64_t&, const double& dist) -> bool { return dist > 100; };
    auto sel_df = df.get_data_by_sel<double, decltype(sel_functor), int, SimpleTime, double, char>(
        "haversine_distance", sel_functor);
    std::cout << "Number of rows that have haversine_distance > 100 KM = "
              << sel_df.get_index().size() << std::endl;

    std::cout << std::endl;
}

void analyze_trip_timestamp(StdDataFrame<uint64_t>& df)
{
    std::cout << "analyze_trip_timestamp()" << std::endl;

    MaxVisitor<SimpleTime> max_visitor;
    MinVisitor<SimpleTime> min_visitor;
    df.multi_visit(std::make_pair("tpep_pickup_datetime", &max_visitor),
                   std::make_pair("tpep_pickup_datetime", &min_visitor));
    std::cout << max_visitor.get_result() << std::endl;
    std::cout << min_visitor.get_result() << std::endl;

    auto& pickup_time_vec = df.get_column<SimpleTime>("tpep_pickup_datetime");
    auto sealed_pickup_time_vec = nu::to_sealed_ds(std::move(pickup_time_vec));

    auto pickup_hour_vec  = nu_make_sharded_vector<char>(sealed_pickup_time_vec.size());
    auto pickup_day_vec   = nu_make_sharded_vector<char>(sealed_pickup_time_vec.size());
    auto pickup_month_vec = nu_make_sharded_vector<char>(sealed_pickup_time_vec.size());
    std::map<char, int> pickup_hour_map;
    std::map<char, int> pickup_day_map;
    std::map<char, int> pickup_month_map;

    for (const auto &time : sealed_pickup_time_vec) {
        pickup_hour_map[time.hour_]++;
        pickup_hour_vec.push_back(time.hour_);
        pickup_day_map[time.day_]++;
        pickup_day_vec.push_back(time.day_);
        pickup_month_map[time.month_]++;
        pickup_month_vec.push_back(time.month_);
    }
    pickup_time_vec = nu::to_unsealed_ds(std::move(sealed_pickup_time_vec));
    df.load_column("pickup_hour", std::move(pickup_hour_vec), nan_policy::dont_pad_with_nans);
    df.load_column("pickup_day", std::move(pickup_day_vec), nan_policy::dont_pad_with_nans);
    df.load_column("pickup_month", std::move(pickup_month_vec), nan_policy::dont_pad_with_nans);

    std::cout << "Print top 10 rows." << std::endl;
    auto top_10_df = df.get_data_by_loc<int, SimpleTime, double, char>(Index2D<long>{0, 9});
    top_10_df.write<std::ostream, int, SimpleTime, double, char>(std::cout, io_format::json);
    std::cout << std::endl;

    for (auto& [hour, cnt] : pickup_hour_map) {
        std::cout << "pickup_hour = " << static_cast<int>(hour) << ", cnt = " << cnt << std::endl;
    }
    std::cout << std::endl;
    for (auto& [day, cnt] : pickup_day_map) {
        std::cout << "pickup_day = " << static_cast<int>(day) << ", cnt = " << cnt << std::endl;
    }
    std::cout << std::endl;
    for (auto& [month, cnt] : pickup_month_map) {
        std::cout << "pickup_month = " << static_cast<int>(month) << ", cnt = " << cnt << std::endl;
    }
    std::cout << std::endl;
}

template <typename T_Key>
void analyze_trip_durations_of_timestamps(StdDataFrame<uint64_t>& df, const char* key_col_name)
{
    std::cout << "analyze_trip_durations_of_timestamps() on key = " << key_col_name << std::endl;
    StdDataFrame<uint64_t> groupby_key = df.groupby1<T_Key>(
        key_col_name, LastVisitor<uint64_t, uint64_t>(),
        std::make_tuple("duration", "med_duration", MedianVisitor<uint64_t>()));
    auto& key_vec      = groupby_key.get_column<T_Key>(key_col_name);
    auto& duration_vec = groupby_key.get_column<uint64_t>("med_duration");
    auto sealed_key_vec = nu::to_sealed_ds(std::move(key_vec));
    auto sealed_duration_vec = nu::to_sealed_ds(std::move(duration_vec));

    auto iter_key = sealed_key_vec.cbegin();
    auto iter_duration = sealed_duration_vec.cbegin();
    for (; iter_key != sealed_key_vec.cend(); ++iter_key, ++iter_duration) {
        std::cout << static_cast<int>(*iter_key) << " " << *iter_duration << std::endl;
    }

    key_vec      = nu::to_unsealed_ds(std::move(sealed_key_vec));
    duration_vec = nu::to_unsealed_ds(std::move(sealed_duration_vec));

    std::cout << std::endl;
}

void do_work()
{
    std::chrono::time_point<std::chrono::steady_clock> times[10];
    auto df  = load_data();
    times[0] = std::chrono::steady_clock::now();
    print_number_vendor_ids_and_unique(df);
    times[1] = std::chrono::steady_clock::now();
    print_passage_counts_by_vendor_id(df, 1);
    times[2] = std::chrono::steady_clock::now();
    print_passage_counts_by_vendor_id(df, 2);
    times[3] = std::chrono::steady_clock::now();
    calculate_trip_duration(df);
    times[4] = std::chrono::steady_clock::now();
    calculate_distribution_store_and_fwd_flag(df);
    times[5] = std::chrono::steady_clock::now();
    calculate_haversine_distance_column(df);
    times[6] = std::chrono::steady_clock::now();
    analyze_trip_timestamp(df);
    times[7] = std::chrono::steady_clock::now();
    analyze_trip_durations_of_timestamps<char>(df, "pickup_day");
    times[8] = std::chrono::steady_clock::now();
    analyze_trip_durations_of_timestamps<char>(df, "pickup_month");
    times[9] = std::chrono::steady_clock::now();

    for (uint32_t i = 1; i < std::size(times); i++) {
        std::cout << "Step " << i << ": "
                  << std::chrono::duration_cast<std::chrono::microseconds>(times[i] - times[i - 1])
                         .count()
                  << " us" << std::endl;
    }
    std::cout << "Total: "
              << std::chrono::duration_cast<std::chrono::microseconds>(times[9] - times[0]).count()
              << " us" << std::endl;
}

int main(int argc, char **argv)
{
    return nu::runtime_main_init(argc, argv, [](int, char**) { do_work(); });
}
