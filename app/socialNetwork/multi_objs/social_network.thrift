namespace cpp social_network
namespace py social_network
namespace lua social_network

struct User {
    1: i64 user_id;
    2: string first_name;
    3: string last_name;
    4: string username;
    5: string password_hashed;
    6: string salt;
}

enum ErrorCode {
  SE_CONNPOOL_TIMEOUT,
  SE_THRIFT_CONN_ERROR,
  SE_UNAUTHORIZED,
  SE_MEMCACHED_ERROR,
  SE_MONGODB_ERROR,
  SE_REDIS_ERROR,
  SE_THRIFT_HANDLER_ERROR,
  SE_RABBITMQ_CONN_ERROR
}

exception ServiceException {
    1: ErrorCode errorCode;
    2: string message;
}

enum PostType {
  POST,
  REPOST,
  REPLY,
  DM
}

struct Media {
  1: i64 media_id;
  2: string media_type;
}

struct Url {
  1: string shortened_url;
  2: string expanded_url;
}

struct UserMention {
  1: i64 user_id;
  2: string username;
}

struct Creator {
  1: i64 user_id;
  2: string username;
}

struct TextServiceReturn {
 1: string text;
 2: list<UserMention> user_mentions;
 3: list<Url> urls;
}

struct Post {
  1: i64 post_id;
  2: Creator creator;
  3: string text;
  4: list<UserMention> user_mentions;
  5: list<Media> media;
  6: list<Url> urls;
  7: i64 timestamp;
  8: PostType post_type;
}

service BackEndService {
  void ComposePost(
    1: string username,
    2: i64 user_id,
    3: string text,
    4: list<i64> media_ids,
    5: list<string> media_types,
    6: PostType post_type
  ) throws (1: ServiceException se)

  list<Post> ReadUserTimeline(
    1: i64 user_id,
    2: i32 start,
    3: i32 stop
  ) throws (1: ServiceException se)

  string Login(
      1: string username,
      2: string password
  ) throws (1: ServiceException se)

  void RegisterUser (
      1: string first_name,
      2: string last_name,
      3: string username,
      4: string password
  ) throws (1: ServiceException se)

  void RegisterUserWithId (
      1: string first_name,
      2: string last_name,
      3: string username,
      4: string password,
      5: i64 user_id
  ) throws (1: ServiceException se)

  list<i64> GetFollowers(
      1: i64 user_id
  ) throws (1: ServiceException se)

  void Unfollow(
      1: i64 user_id,
      2: i64 followee_id
  ) throws (1: ServiceException se)

  void UnfollowWithUsername(
      1: string user_usernmae,
      2: string followee_username
  ) throws (1: ServiceException se)

  void Follow(
      1: i64 user_id,
      2: i64 followee_id
  ) throws (1: ServiceException se)

  void FollowWithUsername(
      1: string user_usernmae,
      2: string followee_username
  ) throws (1: ServiceException se)

  list<i64> GetFollowees(
      1: i64 user_id
  ) throws (1: ServiceException se)

  list<Post> ReadHomeTimeline(
    1: i64 user_id,
    2: i32 start,
    3: i32 stop
  ) throws (1: ServiceException se)

  void UploadMedia(
    1: string filename,
    2: string data
  ) throws (1: ServiceException se)

  string GetMedia(
    1: string filename
  ) throws (1: ServiceException se)

  void RemovePosts(
    1: i64 user_id,
    2: i32 start,
    3: i32 stop
  ) throws (1: ServiceException se)
}
