/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/auth/aws_imds_client.h>
#include <aws/auth/credentials.h>
#include <aws/crt/ImdsClient.h>
#include <aws/crt/auth/Credentials.h>
#include <aws/crt/http/HttpConnection.h>
#include <aws/crt/io/Bootstrap.h>

namespace Aws
{
    namespace Crt
    {
        namespace Imds
        {
            ImdsClient::ImdsClient(const ImdsClientConfig &config, Allocator *allocator) noexcept
            {
                AWS_FATAL_ASSERT(config.Bootstrap != nullptr);

                struct aws_imds_client_options raw_config;
                AWS_ZERO_STRUCT(raw_config);
                raw_config.bootstrap = config.Bootstrap->GetUnderlyingHandle();
                m_client = aws_imds_client_new(allocator, &raw_config);
                m_allocator = allocator;
            }

            ImdsClient::~ImdsClient()
            {
                if (m_client)
                {
                    aws_imds_client_release(m_client);
                    m_client = nullptr;
                }
            }

            template <typename T> struct WrappedCallbackArgs
            {
                WrappedCallbackArgs(Allocator *allocator, T callback, void *userData)
                    : allocator(allocator), callback(callback), userData(userData)
                {
                }
                Allocator *allocator;
                T callback;
                void *userData;
            };

            void ImdsClient::s_onResourceAcquired(const aws_byte_buf *resource, int errorCode, void *userData)
            {
                WrappedCallbackArgs<OnResourceAcquired> *callbackArgs =
                    static_cast<WrappedCallbackArgs<OnResourceAcquired> *>(userData);
                callbackArgs->callback(
                    StringView(reinterpret_cast<char *>(resource->buffer), resource->len),
                    errorCode,
                    callbackArgs->userData);
                Aws::Crt::Delete(callbackArgs, callbackArgs->allocator);
            }

            void ImdsClient::s_onVectorResourceAcquired(const aws_array_list *array, int errorCode, void *userData)
            {
                WrappedCallbackArgs<OnVectorResourceAcquired> *callbackArgs =
                    static_cast<WrappedCallbackArgs<OnVectorResourceAcquired> *>(userData);
                callbackArgs->callback(
                    ArrayListToVector<ByteCursor, StringView>(array, ByteCursorToStringView),
                    errorCode,
                    callbackArgs->userData);
                Aws::Crt::Delete(callbackArgs, callbackArgs->allocator);
            }

            void ImdsClient::s_onCredentialsAcquired(const aws_credentials *credentials, int errorCode, void *userData)
            {
                WrappedCallbackArgs<OnCredentialsAcquired> *callbackArgs =
                    static_cast<WrappedCallbackArgs<OnCredentialsAcquired> *>(userData);
                auto credentialsPtr = Aws::Crt::MakeShared<Auth::Credentials>(callbackArgs->allocator, credentials);
                callbackArgs->callback(credentials, errorCode, callbackArgs->userData);
                Aws::Crt::Delete(callbackArgs, callbackArgs->allocator);
            }

            void ImdsClient::s_onIamProfileAcquired(
                const aws_imds_iam_profile *iamProfileInfo,
                int errorCode,
                void *userData)
            {
                WrappedCallbackArgs<OnIamProfileAcquired> *callbackArgs =
                    static_cast<WrappedCallbackArgs<OnIamProfileAcquired> *>(userData);
                IamProfile iamProfile;
                iamProfile.lastUpdated = aws_date_time_as_epoch_secs(&(iamProfileInfo->last_updated));
                iamProfile.instanceProfileArn = StringView(
                    reinterpret_cast<char *>(iamProfileInfo->instance_profile_arn.ptr),
                    iamProfileInfo->instance_profile_arn.len);
                iamProfile.instanceProfileId = StringView(
                    reinterpret_cast<char *>(iamProfileInfo->instance_profile_id.ptr),
                    iamProfileInfo->instance_profile_id.len);
                callbackArgs->callback(iamProfile, errorCode, callbackArgs->userData);
                Aws::Crt::Delete(callbackArgs, callbackArgs->allocator);
            }

            void ImdsClient::s_onInstanceInfoAcquired(
                const aws_imds_instance_info *instanceInfo,
                int errorCode,
                void *userData)
            {
                WrappedCallbackArgs<OnInstanceInfoAcquired> *callbackArgs =
                    static_cast<WrappedCallbackArgs<OnInstanceInfoAcquired> *>(userData);
                InstanceInfo info;
                info.marketplaceProductCodes = ArrayListToVector<ByteCursor, StringView>(
                    &(instanceInfo->marketplace_product_codes), ByteCursorToStringView);
                info.availabilityZone = StringView(
                    reinterpret_cast<char *>(instanceInfo->availability_zone.ptr), instanceInfo->availability_zone.len);
                info.privateIp =
                    StringView(reinterpret_cast<char *>(instanceInfo->private_ip.ptr), instanceInfo->private_ip.len);
                info.version =
                    StringView(reinterpret_cast<char *>(instanceInfo->version.ptr), instanceInfo->version.len);
                ;
                info.instanceId =
                    StringView(reinterpret_cast<char *>(instanceInfo->instance_id.ptr), instanceInfo->instance_id.len);
                info.billingProducts = ArrayListToVector<ByteCursor, StringView>(
                    &(instanceInfo->billing_products), ByteCursorToStringView);
                info.instanceType = StringView(
                    reinterpret_cast<char *>(instanceInfo->instance_type.ptr), instanceInfo->instance_type.len);
                info.accountId =
                    StringView(reinterpret_cast<char *>(instanceInfo->account_id.ptr), instanceInfo->account_id.len);
                info.imageId =
                    StringView(reinterpret_cast<char *>(instanceInfo->image_id.ptr), instanceInfo->image_id.len);
                info.pendingTime = aws_date_time_as_epoch_secs(&(instanceInfo->pending_time));
                info.architecture = StringView(
                    reinterpret_cast<char *>(instanceInfo->architecture.ptr), instanceInfo->architecture.len);
                info.kernelId =
                    StringView(reinterpret_cast<char *>(instanceInfo->kernel_id.ptr), instanceInfo->kernel_id.len);
                info.ramdiskId =
                    StringView(reinterpret_cast<char *>(instanceInfo->ramdisk_id.ptr), instanceInfo->ramdisk_id.len);
                info.region = StringView(reinterpret_cast<char *>(instanceInfo->region.ptr), instanceInfo->region.len);
                callbackArgs->callback(info, errorCode, callbackArgs->userData);
                Aws::Crt::Delete(callbackArgs, callbackArgs->allocator);
            }

            int ImdsClient::GetResource(const StringView &resourcePath, OnResourceAcquired callback, void *userData)
            {
                auto wrappedCallbackArgs = Aws::Crt::New<WrappedCallbackArgs<OnResourceAcquired>>(
                    m_allocator, m_allocator, callback, userData);
                if (wrappedCallbackArgs == nullptr)
                {
                    return AWS_OP_ERR;
                }

                return aws_imds_client_get_resource_async(
                    m_client, StringViewToByteCursor(resourcePath), s_onResourceAcquired, wrappedCallbackArgs);
            }

            int ImdsClient::GetAmiId(OnResourceAcquired callback, void *userData)
            {
                auto wrappedCallbackArgs = Aws::Crt::New<WrappedCallbackArgs<OnResourceAcquired>>(
                    m_allocator, m_allocator, callback, userData);
                if (wrappedCallbackArgs == nullptr)
                {
                    return AWS_OP_ERR;
                }
                return aws_imds_client_get_ami_id(m_client, s_onResourceAcquired, wrappedCallbackArgs);
            }

            int ImdsClient::GetAmiLaunchIndex(OnResourceAcquired callback, void *userData)
            {
                auto wrappedCallbackArgs = Aws::Crt::New<WrappedCallbackArgs<OnResourceAcquired>>(
                    m_allocator, m_allocator, callback, userData);
                if (wrappedCallbackArgs == nullptr)
                {
                    return AWS_OP_ERR;
                }
                return aws_imds_client_get_ami_launch_index(m_client, s_onResourceAcquired, wrappedCallbackArgs);
            }

            int ImdsClient::GetAmiManifestPath(OnResourceAcquired callback, void *userData)
            {
                auto wrappedCallbackArgs = Aws::Crt::New<WrappedCallbackArgs<OnResourceAcquired>>(
                    m_allocator, m_allocator, callback, userData);
                if (wrappedCallbackArgs == nullptr)
                {
                    return AWS_OP_ERR;
                }
                return aws_imds_client_get_ami_manifest_path(m_client, s_onResourceAcquired, wrappedCallbackArgs);
            }

            int ImdsClient::GetAncestorAmiIds(OnVectorResourceAcquired callback, void *userData)
            {
                auto wrappedCallbackArgs = Aws::Crt::New<WrappedCallbackArgs<OnVectorResourceAcquired>>(
                    m_allocator, m_allocator, callback, userData);
                if (wrappedCallbackArgs == nullptr)
                {
                    return AWS_OP_ERR;
                }
                return aws_imds_client_get_ancestor_ami_ids(m_client, s_onVectorResourceAcquired, wrappedCallbackArgs);
            }

            int ImdsClient::GetInstanceAction(OnResourceAcquired callback, void *userData)
            {
                auto wrappedCallbackArgs = Aws::Crt::New<WrappedCallbackArgs<OnResourceAcquired>>(
                    m_allocator, m_allocator, callback, userData);
                if (wrappedCallbackArgs == nullptr)
                {
                    return AWS_OP_ERR;
                }
                return aws_imds_client_get_instance_action(m_client, s_onResourceAcquired, wrappedCallbackArgs);
            }

            int ImdsClient::GetInstanceId(OnResourceAcquired callback, void *userData)
            {
                auto wrappedCallbackArgs = Aws::Crt::New<WrappedCallbackArgs<OnResourceAcquired>>(
                    m_allocator, m_allocator, callback, userData);
                if (wrappedCallbackArgs == nullptr)
                {
                    return AWS_OP_ERR;
                }
                return aws_imds_client_get_instance_id(m_client, s_onResourceAcquired, wrappedCallbackArgs);
            }

            int ImdsClient::GetInstanceType(OnResourceAcquired callback, void *userData)
            {
                auto wrappedCallbackArgs = Aws::Crt::New<WrappedCallbackArgs<OnResourceAcquired>>(
                    m_allocator, m_allocator, callback, userData);
                if (wrappedCallbackArgs == nullptr)
                {
                    return AWS_OP_ERR;
                }
                return aws_imds_client_get_instance_type(m_client, s_onResourceAcquired, wrappedCallbackArgs);
            }

            int ImdsClient::GetMacAddress(OnResourceAcquired callback, void *userData)
            {
                auto wrappedCallbackArgs = Aws::Crt::New<WrappedCallbackArgs<OnResourceAcquired>>(
                    m_allocator, m_allocator, callback, userData);
                if (wrappedCallbackArgs == nullptr)
                {
                    return AWS_OP_ERR;
                }
                return aws_imds_client_get_mac_address(m_client, s_onResourceAcquired, wrappedCallbackArgs);
            }

            int ImdsClient::GetPrivateIpAddress(OnResourceAcquired callback, void *userData)
            {
                auto wrappedCallbackArgs = Aws::Crt::New<WrappedCallbackArgs<OnResourceAcquired>>(
                    m_allocator, m_allocator, callback, userData);
                if (wrappedCallbackArgs == nullptr)
                {
                    return AWS_OP_ERR;
                }
                return aws_imds_client_get_private_ip_address(m_client, s_onResourceAcquired, wrappedCallbackArgs);
            }

            int ImdsClient::GetAvailabilityZone(OnResourceAcquired callback, void *userData)
            {
                auto wrappedCallbackArgs = Aws::Crt::New<WrappedCallbackArgs<OnResourceAcquired>>(
                    m_allocator, m_allocator, callback, userData);
                if (wrappedCallbackArgs == nullptr)
                {
                    return AWS_OP_ERR;
                }
                return aws_imds_client_get_availability_zone(m_client, s_onResourceAcquired, wrappedCallbackArgs);
            }

            int ImdsClient::GetProductCodes(OnResourceAcquired callback, void *userData)
            {
                auto wrappedCallbackArgs = Aws::Crt::New<WrappedCallbackArgs<OnResourceAcquired>>(
                    m_allocator, m_allocator, callback, userData);
                if (wrappedCallbackArgs == nullptr)
                {
                    return AWS_OP_ERR;
                }
                return aws_imds_client_get_product_codes(m_client, s_onResourceAcquired, wrappedCallbackArgs);
            }

            int ImdsClient::GetPublicKey(OnResourceAcquired callback, void *userData)
            {
                auto wrappedCallbackArgs = Aws::Crt::New<WrappedCallbackArgs<OnResourceAcquired>>(
                    m_allocator, m_allocator, callback, userData);
                if (wrappedCallbackArgs == nullptr)
                {
                    return AWS_OP_ERR;
                }
                return aws_imds_client_get_public_key(m_client, s_onResourceAcquired, wrappedCallbackArgs);
            }

            int ImdsClient::GetRamDiskId(OnResourceAcquired callback, void *userData)
            {
                auto wrappedCallbackArgs = Aws::Crt::New<WrappedCallbackArgs<OnResourceAcquired>>(
                    m_allocator, m_allocator, callback, userData);
                if (wrappedCallbackArgs == nullptr)
                {
                    return AWS_OP_ERR;
                }
                return aws_imds_client_get_ramdisk_id(m_client, s_onResourceAcquired, wrappedCallbackArgs);
            }

            int ImdsClient::GetReservationId(OnResourceAcquired callback, void *userData)
            {
                auto wrappedCallbackArgs = Aws::Crt::New<WrappedCallbackArgs<OnResourceAcquired>>(
                    m_allocator, m_allocator, callback, userData);
                if (wrappedCallbackArgs == nullptr)
                {
                    return AWS_OP_ERR;
                }
                return aws_imds_client_get_reservation_id(m_client, s_onResourceAcquired, wrappedCallbackArgs);
            }

            int ImdsClient::GetSecurityGroups(OnVectorResourceAcquired callback, void *userData)
            {
                auto wrappedCallbackArgs = Aws::Crt::New<WrappedCallbackArgs<OnVectorResourceAcquired>>(
                    m_allocator, m_allocator, callback, userData);
                if (wrappedCallbackArgs == nullptr)
                {
                    return AWS_OP_ERR;
                }
                return aws_imds_client_get_security_groups(m_client, s_onVectorResourceAcquired, wrappedCallbackArgs);
            }

            int ImdsClient::GetBlockDeviceMapping(OnVectorResourceAcquired callback, void *userData)
            {
                auto wrappedCallbackArgs = Aws::Crt::New<WrappedCallbackArgs<OnVectorResourceAcquired>>(
                    m_allocator, m_allocator, callback, userData);
                if (wrappedCallbackArgs == nullptr)
                {
                    return AWS_OP_ERR;
                }
                return aws_imds_client_get_block_device_mapping(
                    m_client, s_onVectorResourceAcquired, wrappedCallbackArgs);
            }

            int ImdsClient::GetAttachedIamRole(OnResourceAcquired callback, void *userData)
            {
                auto wrappedCallbackArgs = Aws::Crt::New<WrappedCallbackArgs<OnResourceAcquired>>(
                    m_allocator, m_allocator, callback, userData);
                if (wrappedCallbackArgs == nullptr)
                {
                    return AWS_OP_ERR;
                }
                return aws_imds_client_get_attached_iam_role(m_client, s_onResourceAcquired, wrappedCallbackArgs);
            }

            int ImdsClient::GetCredentials(
                const StringView &iamRoleName,
                OnCredentialsAcquired callback,
                void *userData)
            {
                auto wrappedCallbackArgs = Aws::Crt::New<WrappedCallbackArgs<OnCredentialsAcquired>>(
                    m_allocator, m_allocator, callback, userData);
                if (wrappedCallbackArgs == nullptr)
                {
                    return AWS_OP_ERR;
                }
                return aws_imds_client_get_credentials(
                    m_client, StringViewToByteCursor(iamRoleName), s_onCredentialsAcquired, wrappedCallbackArgs);
            }

            int ImdsClient::GetIamProfile(OnIamProfileAcquired callback, void *userData)
            {
                auto wrappedCallbackArgs = Aws::Crt::New<WrappedCallbackArgs<OnIamProfileAcquired>>(
                    m_allocator, m_allocator, callback, userData);
                if (wrappedCallbackArgs == nullptr)
                {
                    return AWS_OP_ERR;
                }
                return aws_imds_client_get_iam_profile(m_client, s_onIamProfileAcquired, wrappedCallbackArgs);
            }

            int ImdsClient::GetUserData(OnResourceAcquired callback, void *userData)
            {
                auto wrappedCallbackArgs = Aws::Crt::New<WrappedCallbackArgs<OnResourceAcquired>>(
                    m_allocator, m_allocator, callback, userData);
                if (wrappedCallbackArgs == nullptr)
                {
                    return AWS_OP_ERR;
                }
                return aws_imds_client_get_user_data(m_client, s_onResourceAcquired, wrappedCallbackArgs);
            }

            int ImdsClient::GetInstanceSignature(OnResourceAcquired callback, void *userData)
            {
                auto wrappedCallbackArgs = Aws::Crt::New<WrappedCallbackArgs<OnResourceAcquired>>(
                    m_allocator, m_allocator, callback, userData);
                if (wrappedCallbackArgs == nullptr)
                {
                    return AWS_OP_ERR;
                }
                return aws_imds_client_get_instance_signature(m_client, s_onResourceAcquired, wrappedCallbackArgs);
            }

            int ImdsClient::GetInstanceInfo(OnInstanceInfoAcquired callback, void *userData)
            {
                auto wrappedCallbackArgs = Aws::Crt::New<WrappedCallbackArgs<OnInstanceInfoAcquired>>(
                    m_allocator, m_allocator, callback, userData);
                if (wrappedCallbackArgs == nullptr)
                {
                    return AWS_OP_ERR;
                }
                return aws_imds_client_get_instance_info(m_client, s_onInstanceInfoAcquired, wrappedCallbackArgs);
            }
        } // namespace Imds
    }     // namespace Crt

} // namespace Aws
